A linux based qt6 and openGL3.3 Parametric CAD with storyboard B-rep rappresentation of model with proprietary kernel.
Code written entirely by AI.
- Build ed esecuzione:
  cmake -S . -B forgecad-cuda-build -DCMAKE_BUILD_TYPE=Debug   # the first step
  cmake --build forgecad-cuda-build -j
  ./forgecad-cuda-build/forgecad
