using UnityEngine;
using System.Runtime.InteropServices;
using UnityEngine.InputSystem; // [추가] 새로운 Input System 사용

public class AceTest : MonoBehaviour
{
    [DllImport("NVIDIA-Ace")]
    private static extern int StartSTT(string modelPath);

    [DllImport("NVIDIA-Ace")]
    private static extern void PushAudioData([In] float[] pAudioData, int sampleCount);

    [DllImport("NVIDIA-Ace")]
    private static extern System.IntPtr GetSTTResult(); // 문자열을 안전하게 받기 위해 IntPtr 사용

    [DllImport("NVIDIA-Ace")] 
    private static extern int StartGPT(string modelPath);
    [DllImport("NVIDIA-Ace", CharSet = CharSet.Ansi)] 
    private static extern void PushTextToGPT(string inputText);
    [DllImport("NVIDIA-Ace")] 
    private static extern System.IntPtr GetGPTResult();


    private AudioClip micClip;
    private int lastSamplePos = 0;
    private float[] tempBuffer = new float[4096]; // 데이터를 담을 바구니
    private const int SampleRate = 16000; // Whisper 모델 표준
    private string lastPrintedText = "";
    private bool isReady = false;       // 초기화 완료 여부
    private bool isRecording = false;
    private int startSamplePos = 0;
    private string targetMicName; // 변수 이름 통일
    private AudioSource debugAudioSource;

    void Start()
    {
        if (Microphone.devices.Length == 0) return;
        targetMicName = Microphone.devices[0];

        string path = System.IO.Path.GetFullPath(System.IO.Path.Combine(Application.streamingAssetsPath, "Models"));

        // STT와 GPT를 동시에 초기화
        int sttRes = StartSTT(path);
        int gptRes = StartGPT(path);

        if (sttRes == 0 && gptRes == 0)
        {
            Debug.Log($"🎉 STT 및 GPT 엔진 초기화 완료! (마이크: {targetMicName})");
            isReady = true;
        }
        else
        {
            Debug.LogError($"초기화 실패 - STT: {sttRes}, GPT: {gptRes}");
        }
    }



    void Update()
    {
        if (!isReady) return;

        if (Keyboard.current.spaceKey.wasPressedThisFrame)
        {
            micClip = Microphone.Start(targetMicName, false, 10, 16000);
            isRecording = true;
            Debug.Log("🎙️ 플레이어 음성 듣는 중... (스페이스바를 떼면 번역 후 NPC가 생각합니다)");
        }

        if (Keyboard.current.spaceKey.wasReleasedThisFrame && isRecording)
        {
            isRecording = false;
            int pos = Microphone.GetPosition(targetMicName);
            if (pos > 0)
            {
                float[] samples = new float[pos];
                micClip.GetData(samples, 0);
                Microphone.End(targetMicName);

                float[] finalData = Resample(samples, micClip.frequency, 16000);

                // 1. 마이크 소리를 STT로 전송
                PushAudioData(finalData, finalData.Length);

                // 2. STT 결과 확인
                System.IntPtr sttPtr = GetSTTResult();
                if (sttPtr != System.IntPtr.Zero)
                {
                    string recognizedText = Marshal.PtrToStringUTF8(sttPtr);
                    if (!string.IsNullOrEmpty(recognizedText))
                    {
                        Debug.Log($"<color=white>나(Player): {recognizedText}</color>");

                        // 3. 번역된 내 문장을 GPT의 뇌로 밀어넣기!
                        Debug.Log("🧠 NPC 생각 중...");
                        PushTextToGPT(recognizedText);

                        // 4. GPT가 내놓은 최종 대답 확인
                        System.IntPtr gptPtr = GetGPTResult();
                        if (gptPtr != System.IntPtr.Zero)
                        {
                            string gptResponse = Marshal.PtrToStringUTF8(gptPtr);
                            if (!string.IsNullOrEmpty(gptResponse))
                            {
                                Debug.Log($"<color=cyan>NPC: {gptResponse}</color>");
                            }
                        }
                    }
                }
            }
        }
    }
    float[] Resample(float[] src, int srcRate, int dstRate)
    {
        if (srcRate == dstRate) return src;
        int dstLen = Mathf.RoundToInt((float)src.Length * dstRate / srcRate);
        float[] dst = new float[dstLen];
        for (int i = 0; i < dstLen; i++)
        {
            float t = (float)i * srcRate / dstRate;
            int idx = (int)t;
            if (idx + 1 < src.Length) dst[i] = Mathf.Lerp(src[idx], src[idx + 1], t - idx);
            else dst[i] = src[idx];
        }
        return dst;
    }
}