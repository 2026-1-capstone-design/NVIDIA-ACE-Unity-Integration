pushd "%~dp0"
curl -L "https://developer.nvidia.com/downloads/assets/ace/model_zip/nemotron-mini-4b-instruct-AIM-GGUF.zip" -o out.zip
tar -x -f out.zip -O "{8E31808B-C182-4016-9ED8-64804FF5B40D}/nemotron-4-mini-4b-instruct_q4_0.gguf" > nemotron-4-mini-4b-instruct_q4_0.gguf
tar -x -f out.zip -O "{8E31808B-C182-4016-9ED8-64804FF5B40D}/NVIDIA Software and Model Evaluation License Agreement (2024.06.28).txt" > "NVIDIA Software and Model Evaluation License Agreement (2024.06.28).txt"
del out.zip
popd
IF NOT "%1"=="-nopause" (
	pause
)
