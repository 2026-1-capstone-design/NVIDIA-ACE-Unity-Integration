// NVIGI_Bridge.cpp
//#define _CRT_SECURE_NO_WARNINGS

#include "pch.h"
#include "nvigi.h" // SDK 헤더
#include "nvigi_asr_whisper.h"
#include "nvigi_gpt.h"
#include <vector>
#include <string>
#include <stdio.h> // 파일 입출력


#define EXPORT_API __declspec(dllexport)


nvigi::IAutoSpeechRecognition* g_asrInterface = nullptr;
nvigi::InferenceInstance* g_asrInstance = nullptr;
std::string g_latestSTTResult = "";

nvigi::IGeneralPurposeTransformer* g_gptInterface = nullptr;
nvigi::InferenceInstance* g_gptInstance = nullptr;
std::string g_latestGPTResult = "";

bool g_isNvigiInitialized = false;
const char* g_pluginPaths[] = { ".", "Assets/Plugins/x86_64" };
nvigi::Preferences g_pref{};

nvigi::InferenceExecutionState STTCallback(const nvigi::InferenceExecutionContext* context, nvigi::InferenceExecutionState state, void* userData) {
    FILE* f = nullptr;
    if (fopen_s(&f, "nvigi_log.txt", "a") == 0 && f != nullptr) {
        fprintf(f, "[STT Callback] 엔진에서 응답이 왔습니다! 상태(state): %d\n", (int)state);

        if (context && context->outputs) {
            nvigi::InferenceDataText* textData = nullptr;
            if (context->outputs->findAndValidateSlot<nvigi::InferenceDataText>(nvigi::kASRWhisperDataSlotTranscribedText, &textData)) {
                if (textData) {
                    const char* utf8Text = textData->getUTF8Text();
                    fprintf(f, "  -> 텍스트 데이터 확보! 내용: '%s'\n", utf8Text ? utf8Text : "NULL");

                    if (utf8Text && strlen(utf8Text) > 0) {
                        g_latestSTTResult = utf8Text;
                        fprintf(f, "\n[🎉 자막 추출 대성공!] %s\n\n", utf8Text);
                    }
                }
            }
        }
        fclose(f);
    }
    return state;
}

nvigi::InferenceExecutionState GPTCallback(const nvigi::InferenceExecutionContext* context, nvigi::InferenceExecutionState state, void* userData) {
    if (context && context->outputs) {
        nvigi::InferenceDataText* textData = nullptr;
        // GPT는 스트리밍 형태로 단어(토큰)를 쪼개서 뱉어내므로, 응답 슬롯(kGPTDataSlotResponse)에서 텍스트를 찾아 이어붙입니다.
        if (context->outputs->findAndValidateSlot<nvigi::InferenceDataText>(nvigi::kGPTDataSlotResponse, &textData)) {
            if (textData) {
                const char* utf8Text = textData->getUTF8Text();
                if (utf8Text && strlen(utf8Text) > 0) g_latestGPTResult += utf8Text;
            }
        }
    }
    return state;
}

void LogCallback(nvigi::LogType type, const char* msg) {
    FILE* f = nullptr;
    // 2. fopen_s를 사용하여 보안 경고(C4996) 해결
    if (fopen_s(&f, "nvigi_log.txt", "a") == 0 && f != nullptr) {
        fprintf(f, "[NVIGI LOG] %s\n", msg);
        fclose(f);
    }
}

extern "C" {
    // NVIGI 초기화 함수
    EXPORT_API int InitializeNVIGI(const char* modelPath) {
        // SDK의 초기화 로직 호출
        // return nvigi_init(modelPath); 
        return 0;
    }

    // 텍스트를 받아서 음성 데이터(TTS) 주소를 반환하는 예시
    EXPORT_API void ProcessTTS(const char* text, float* outBuffer) {
        // NVIGI TTS 모델 추론 로직
    }

    // 1. STT 엔진 시작 (초기화)
    EXPORT_API int StartSTT(const char* modelPath) {

        FILE* f = nullptr;
        if (fopen_s(&f, "nvigi_log.txt", "w") == 0 && f != nullptr) {
            fprintf(f, "--- NVIGI Init Start ---\nReceived Path: %s\n", modelPath);
            fclose(f);
        }

        if (g_asrInstance != nullptr) {             // unity 재실행 시 엔진 중복 초기화 방지
            return 0;
        }

        if (!g_isNvigiInitialized) {
            g_pref.logLevel = nvigi::LogLevel::eVerbose;
            g_pref.utf8PathsToPlugins = g_pluginPaths;
            g_pref.numPathsToPlugins = 2;
            g_pref.logMessageCallback = LogCallback;

            if (nvigiInit(g_pref) != nvigi::kResultOk) return -1;
            g_isNvigiInitialized = true;
        }

        if (nvigiLoadInterface(nvigi::plugin::asr::ggml::cuda::kId, nvigi::IAutoSpeechRecognition::s_type, 1, (void**)&g_asrInterface, nullptr) != nvigi::kResultOk) {
            if (nvigiLoadInterface(nvigi::plugin::asr::ggml::cpu::kId, nvigi::IAutoSpeechRecognition::s_type, 1, (void**)&g_asrInterface, nullptr) != nvigi::kResultOk) return -2;
        }

        static std::string safeModelPathSTT = modelPath;
        static nvigi::CommonCreationParameters commonSTT{};
        commonSTT.utf8PathToModels = safeModelPathSTT.c_str();
        commonSTT.modelGUID = "{5CAD3A03-1272-4D43-9F3D-655417526170}";

        static nvigi::ASRWhisperCreationParameters whisperParams{};
        whisperParams.language = "ko";
        whisperParams.detectLanguage = false;

        if (whisperParams.chain((nvigi::BaseStructure*)&commonSTT) != nvigi::kResultOk) return -4;
        if (g_asrInterface == nullptr) return -5;
        if (g_asrInterface->createInstance((nvigi::NVIGIParameter*)&whisperParams, &g_asrInstance) != nvigi::kResultOk) return -3;
        return 0;
    }

    // 2. 실시간 음성 데이터 전달
    EXPORT_API void PushAudioData(float* pAudioData, int sampleCount) {
        if (!g_asrInstance || !pAudioData || sampleCount <= 0) return;

        g_latestSTTResult = "";

        nvigi::CpuData cpuAudio(sampleCount * sizeof(float), pAudioData);

        nvigi::InferenceDataAudio audioData((nvigi::NVIGIParameter*)&cpuAudio);
        audioData.samplingRate = 16000;
        audioData.bitsPerSample = 32; // float는 무조건 32bit
        audioData.channels = 1;
        audioData.dataType = nvigi::AudioDataType::eRawFP32; // 순수 float 형식

        nvigi::InferenceDataSlot inputSlot(nvigi::kASRWhisperDataSlotAudio, (nvigi::NVIGIParameter*)&audioData);
        nvigi::InferenceDataSlotArray inputs(1, &inputSlot);

        nvigi::InferenceDataSlot outputSlot(nvigi::kASRWhisperDataSlotTranscribedText, (nvigi::NVIGIParameter*)nullptr);
        nvigi::InferenceDataSlotArray outputs(1, &outputSlot);


        nvigi::ASRWhisperRuntimeParameters whisperRun{};
        whisperRun.suppressBlank = false;
        whisperRun.noSpeechThold = 0.01f;

        nvigi::StreamingParameters streamParams{};
        streamParams.mode = nvigi::StreamingMode::eStreamingModeInputOutput;
        streamParams.signal = nvigi::StreamSignal::eStreamSignalStop;

        whisperRun.chain((nvigi::BaseStructure*)&streamParams);

        nvigi::InferenceExecutionContext context{};
        context.instance = g_asrInstance;
        context.inputs = &inputs;
        context.outputs = &outputs;
        context.callback = STTCallback;
        context.runtimeParameters = (nvigi::NVIGIParameter*)&whisperRun;

        g_asrInstance->evaluate(&context);
    }

    // 3. 변환된 텍스트 가져오기
    EXPORT_API const char* GetSTTResult() {
        return g_latestSTTResult.c_str();
    }

    EXPORT_API int StartGPT(const char* modelPath) {
        if (g_gptInstance != nullptr) return 0;

        // 1. 프레임워크 초기화 (STT가 먼저 켰다면 안전하게 패스됨)
        if (!g_isNvigiInitialized) {
            g_pref.logLevel = nvigi::LogLevel::eVerbose;
            g_pref.utf8PathsToPlugins = g_pluginPaths;
            g_pref.numPathsToPlugins = 2;
            if (nvigiInit(g_pref) != nvigi::kResultOk) return -1;
            g_isNvigiInitialized = true;
        }

        FILE* f = nullptr;
        if (fopen_s(&f, "nvigi_log.txt", "a") == 0 && f) { fprintf(f, "\n[GPT 추적] 1. 인터페이스 로드 시도...\n"); fclose(f); }

        if (nvigiLoadInterface(nvigi::plugin::gpt::ggml::cuda::kId, nvigi::IGeneralPurposeTransformer::s_type, 1, (void**)&g_gptInterface, nullptr) != nvigi::kResultOk) {
            if (nvigiLoadInterface(nvigi::plugin::gpt::ggml::cpu::kId, nvigi::IGeneralPurposeTransformer::s_type, 1, (void**)&g_gptInterface, nullptr) != nvigi::kResultOk) return -2;
        }

        if (fopen_s(&f, "nvigi_log.txt", "a") == 0 && f) { fprintf(f, "[GPT 추적] 2. 파라미터 조립...\n"); fclose(f); }

        static std::string safeModelPathGPT = modelPath;
        static nvigi::CommonCreationParameters commonGPT{};
        commonGPT.utf8PathToModels = safeModelPathGPT.c_str();
        commonGPT.modelGUID = "{8E31808B-C182-4016-9ED8-64804FF5B40D}";

        static nvigi::GPTCreationParameters gptParams{};
        gptParams.contextSize = 2048;
        gptParams.maxNumTokensToPredict = 512;



        if (fopen_s(&f, "nvigi_log.txt", "a") == 0 && f) {
            fprintf(f, "[GPT 추적] 3. createInstance 호출! (만약 여기서 로그가 끊기고 튕긴다면 100%% 모델 경로/파일 누락 문제입니다!)\n");
            fclose(f);
        }

        // 🛑 여기서 튕긴다면 모델 폴더 배치가 잘못된 것입니다!
        if (gptParams.chain((nvigi::BaseStructure*)&commonGPT) != nvigi::kResultOk) return -4;
        if (g_gptInterface == nullptr) return -5;
        if (g_gptInterface->createInstance((nvigi::NVIGIParameter*)&gptParams, &g_gptInstance) != nvigi::kResultOk) return -3;
        return 0;
    }

    EXPORT_API void PushTextToGPT(const char* inputText) {
        if (!g_gptInstance || !inputText) return;
        g_latestGPTResult = ""; // 이전 답변 초기화

        // 텍스트 데이터를 엔진 전용 바구니로 포장
        nvigi::CpuData cpuText(strlen(inputText) + 1, (void*)inputText);
        nvigi::InferenceDataText textData((nvigi::NVIGIParameter*)&cpuText);

        // 사용자(Player)의 텍스트 슬롯 설정
        nvigi::InferenceDataSlot inputSlot(nvigi::kGPTDataSlotUser, (nvigi::NVIGIParameter*)&textData);
        nvigi::InferenceDataSlotArray inputs(1, &inputSlot);

        // AI(Assistant)의 대답을 받을 슬롯 준비
        nvigi::InferenceDataSlot outputSlot(nvigi::kGPTDataSlotResponse, (nvigi::NVIGIParameter*)nullptr);
        nvigi::InferenceDataSlotArray outputs(1, &outputSlot);

        // 런타임 설정 (대화형 모드 켜기)
        nvigi::GPTRuntimeParameters gptRunParams{};
        gptRunParams.interactive = false;

        nvigi::InferenceExecutionContext context{};
        context.instance = g_gptInstance;
        context.inputs = &inputs;
        context.outputs = &outputs;
        context.callback = GPTCallback;
        context.runtimeParameters = (nvigi::NVIGIParameter*)&gptRunParams;

        // GPT 엔진 가동 (생각 시작)
        g_gptInstance->evaluate(&context);
    }

    EXPORT_API const char* GetGPTResult() { return g_latestGPTResult.c_str(); }

}