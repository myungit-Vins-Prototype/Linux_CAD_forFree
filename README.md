A linux based qt6 and openGL3.3 Parametric CAD with storyboard B-rep rappresentation of model with proprietary kernel.
Code written entirely by AI.
It's developed with NVIDIA CUDA support for various geometry and tasselation calculation.
- Build ed esecuzione
  cmake -S . -B forgecad-cuda-build -DCMAKE_BUILD_TYPE=Debug   # solo la prima volta
  cmake --build forgecad-cuda-build -j
  ./forgecad-cuda-build/forgecad
