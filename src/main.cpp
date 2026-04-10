#define NOMINMAX
#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <cs8reader.h>
#include <core.h>
#include <gui/widgets/file_select.h>
#include <filesystem>
#include <regex>
#include <gui/tuner.h>
#include <algorithm>
#include <stdexcept>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "cs8_file_source",
    /* Description:     */ "CS8 file source module for SDR++",
    /* Author:          */ "BUSH222",
    /* Version:         */ 0, 1, 1,
    /* Max instances    */ 1
};

ConfigManager config;

class CS8FileSourceModule : public ModuleManager::Instance {
public:
    CS8FileSourceModule(std::string name) : fileSelect("", { "CS8 IQ Files (*.cs8)", "*.cs8", "Raw Files (*.raw)", "*.raw", "All Files", "*" }) {
        this->name = name;

        if (core::args["server"].b()) { return; }

        config.acquire();
        fileSelect.setPath(config.conf["path"], true);
        if (config.conf.contains("sampleRate")) {
            sampleRate = config.conf["sampleRate"];
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
        sigpath::sourceManager.registerSource("CS8 File", &handler);
    }

    ~CS8FileSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("CS8 File");
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
        CS8FileSourceModule* _this = (CS8FileSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", _this->centerFreq);
        sigpath::iqFrontEnd.setBuffering(false);
        gui::waterfall.centerFrequencyLocked = true;
        //gui::freqSelect.minFreq = _this->centerFreq - (_this->sampleRate/2);
        //gui::freqSelect.maxFreq = _this->centerFreq + (_this->sampleRate/2);
        //gui::freqSelect.limitFreq = true;
        flog::info("CS8FileSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        CS8FileSourceModule* _this = (CS8FileSourceModule*)ctx;
        sigpath::iqFrontEnd.setBuffering(true);
        //gui::freqSelect.limitFreq = false;
        gui::waterfall.centerFrequencyLocked = false;
        flog::info("CS8FileSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        CS8FileSourceModule* _this = (CS8FileSourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->reader == NULL) { return; }
        _this->running = true;
        _this->workerThread = _this->float32Mode ? std::thread(floatWorker, _this) : std::thread(worker, _this);
        flog::info("CS8FileSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        CS8FileSourceModule* _this = (CS8FileSourceModule*)ctx;
        if (!_this->running) { return; }
        if (_this->reader == NULL) { return; }
        _this->stream.stopWriter();
        if (_this->workerThread.joinable()) {
            _this->workerThread.join();
        }
        _this->stream.clearWriteStop();
        _this->running = false;
        _this->reader->rewind();
        flog::info("CS8FileSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        CS8FileSourceModule* _this = (CS8FileSourceModule*)ctx;
        flog::info("CS8FileSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        CS8FileSourceModule* _this = (CS8FileSourceModule*)ctx;

        if (ImGui::InputFloat("Sample Rate", &_this->sampleRate, 0.0f, 0.0f, "%.0f")) {
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
                    _this->reader = new CS8Reader(_this->fileSelect.path);
                    if (!_this->reader->isValid()) {
                        throw std::runtime_error("Could not open file");
                    }
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

        ImGui::Checkbox("Float32 Mode##_cs8_file_source", &_this->float32Mode);
    }

    static void worker(void* ctx) {
        CS8FileSourceModule* _this = (CS8FileSourceModule*)ctx;
        double sampleRate = std::max(_this->sampleRate, 1.0f);
        int blockSize = std::min((int)(sampleRate / 200.0f), (int)STREAM_BUFFER_SIZE);
        int8_t* inBuf = new int8_t[blockSize * 2];

        while (true) {
            _this->reader->readSamples(inBuf, blockSize * 2 * sizeof(int8_t));
            volk_8i_s32f_convert_32f((float*)_this->stream.writeBuf, inBuf, 128.0f, blockSize * 2);
            if (!_this->stream.swap(blockSize)) { break; };
        }

        delete[] inBuf;
    }

    static void floatWorker(void* ctx) {
        CS8FileSourceModule* _this = (CS8FileSourceModule*)ctx;
        double sampleRate = std::max(_this->sampleRate, 1.0f);
        int blockSize = std::min((int)(sampleRate / 200.0f), (int)STREAM_BUFFER_SIZE);
        dsp::complex_t* inBuf = new dsp::complex_t[blockSize];

        while (true) {
            _this->reader->readSamples(_this->stream.writeBuf, blockSize * sizeof(dsp::complex_t));
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
    CS8Reader* reader = NULL;
    bool running = false;
    bool enabled = true;
    float sampleRate = 1000000;
    std::thread workerThread;

    double centerFreq = 100000000;

    bool float32Mode = false;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["path"] = "";
    def["sampleRate"] = 1000000.0f;
    config.setPath(core::args["root"].s() + "/cs8_file_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new CS8FileSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (CS8FileSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
