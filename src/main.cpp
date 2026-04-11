#define NOMINMAX
#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <better_file_reader.h>
#include <core.h>
#include <gui/style.h>
#include <gui/widgets/file_select.h>
#include <filesystem>
#include <regex>
#include <gui/tuner.h>
#include <algorithm>
#include <stdexcept>

#define CONCAT(a, b) ((std::string(a) + b).c_str())
#define MIN_SAMPLE_RATE 8000.0f

SDRPP_MOD_INFO{
    /* Name:            */ "better_file_source",
    /* Description:     */ "Better file source module for SDR++",
    /* Author:          */ "BUSH222",
    /* Version:         */ 0, 1, 1,
    /* Max instances    */ 1
};

ConfigManager config;

class BetterFileSourceModule : public ModuleManager::Instance {
public:
    BetterFileSourceModule(std::string name) : fileSelect("", { "CS8 IQ Files (*.cs8)", "*.cs8", "CS8 IQ Files (*.c8)", "*.c8", "Raw Files (*.raw)", "*.raw", "All Files", "*" }) {
        this->name = name;

        if (core::args["server"].b()) { return; }

        config.acquire();
        fileSelect.setPath(config.conf["path"], true);
        if (config.conf.contains("sampleRate")) {
            sampleRate = std::max((float)config.conf["sampleRate"], MIN_SAMPLE_RATE);
        }
        config.release();

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;
        sigpath::sourceManager.registerSource("Better File", &handler);
    }

    ~BetterFileSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("Better File");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static void menuSelected(void* ctx) {
        BetterFileSourceModule* _this = (BetterFileSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", _this->centerFreq);
        sigpath::iqFrontEnd.setBuffering(false);
        gui::waterfall.centerFrequencyLocked = true;
        flog::info("BetterFileSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        BetterFileSourceModule* _this = (BetterFileSourceModule*)ctx;
        sigpath::iqFrontEnd.setBuffering(true);
        gui::waterfall.centerFrequencyLocked = false;
        flog::info("BetterFileSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        BetterFileSourceModule* _this = (BetterFileSourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->reader == NULL) { return; }
        _this->running = true;
        _this->workerThread = std::thread(worker, _this);
        flog::info("BetterFileSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        BetterFileSourceModule* _this = (BetterFileSourceModule*)ctx;
        if (!_this->running) { return; }
        if (_this->reader == NULL) { return; }
        _this->stream.stopWriter();
        if (_this->workerThread.joinable()) {
            _this->workerThread.join();
        }
        _this->stream.clearWriteStop();
        _this->running = false;
        _this->reader->rewind();
        flog::info("BetterFileSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        BetterFileSourceModule* _this = (BetterFileSourceModule*)ctx;
        flog::info("BetterFileSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        BetterFileSourceModule* _this = (BetterFileSourceModule*)ctx;

        float menuWidth = ImGui::GetContentRegionAvail().x;

        ImGui::LeftLabel("Sample Rate");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputFloat(("##_sample_rate_" + _this->name).c_str(), &_this->sampleRate, 1.0f, 100.0f, "%.0f")) {
            _this->sampleRate = std::max(_this->sampleRate, MIN_SAMPLE_RATE);
            config.acquire();
            config.conf["sampleRate"] = _this->sampleRate;
            config.release(true);
            if (_this->reader) {
                _this->reader->setSampleRate(_this->sampleRate);
                core::setInputSampleRate(_this->sampleRate);
            }
        }

        if (_this->fileSelect.render("##file_source_" + _this->name)) {
            if (_this->fileSelect.pathIsValid()) {
                if (_this->reader != NULL) {
                    _this->reader->close();
                    delete _this->reader;
                }
                try {
                    _this->reader = new BetterFileReader(_this->fileSelect.path);
                    if (!_this->reader->isValid()) {
                        throw std::runtime_error("Could not open file");
                    }
                    // Enforce minimum sample rate of 8000 Hz
                    _this->sampleRate = std::max(_this->sampleRate, MIN_SAMPLE_RATE);
                    _this->reader->setSampleRate(_this->sampleRate);
                    core::setInputSampleRate(_this->sampleRate);
                    std::string filename = std::filesystem::path(_this->fileSelect.path).filename().string();
                    _this->centerFreq = _this->getFrequency(filename);
                    tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", _this->centerFreq);
                }
                catch (const std::exception& e) {
                    flog::error("Error: {}", e.what());
                }
                config.acquire();
                config.conf["path"] = _this->fileSelect.path;
                config.conf["sampleRate"] = _this->sampleRate;
                config.release(true);
            }
        }

    }

    static void worker(void* ctx) {
        BetterFileSourceModule* _this = (BetterFileSourceModule*)ctx;
        double sampleRate = std::max(_this->sampleRate, MIN_SAMPLE_RATE);
        int blockSize = std::min((int)(sampleRate / 200.0f), (int)STREAM_BUFFER_SIZE);
        int8_t* inBuf = new int8_t[blockSize * 2];

        while (true) {
            _this->reader->readSamples(inBuf, blockSize * 2 * sizeof(int8_t));
            volk_8i_s32f_convert_32f((float*)_this->stream.writeBuf, inBuf, 128.0f, blockSize * 2);
            if (!_this->stream.swap(blockSize)) { break; };
        }

        delete[] inBuf;
    }

    double getFrequency(std::string filename) {
        std::regex expr("[0-9]+Hz");
        std::smatch matches;
        std::regex_search(filename, matches, expr);
        if (matches.empty()) { return 0; }
        std::string freqStr = matches[0].str();
        return std::atof(freqStr.substr(0, freqStr.size() - 2).c_str());
    }

    FileSelect fileSelect;
    std::string name;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    BetterFileReader* reader = NULL;
    bool running = false;
    bool enabled = true;
    float sampleRate = 1000000;
    std::thread workerThread;

    double centerFreq = 100000000;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["path"] = "";
    def["sampleRate"] = 1000000.0f;
    config.setPath(core::args["root"].s() + "/better_file_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new BetterFileSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (BetterFileSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
