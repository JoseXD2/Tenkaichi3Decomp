#!/bin/bash
# Builds the Windows release zip inside MSYS2 CLANG64. Started by build_windows.bat. Log: build_log.txt
cd "$(dirname "$0")"
exec > >(tee build_log.txt) 2>&1
grep -q 'enable-bracketed-paste off' ~/.inputrc 2>/dev/null || echo 'set enable-bracketed-paste off' >> ~/.inputrc
echo "== 1/5 instalando ferramentas (pacman)"
pacman -Syu --noconfirm
pacman -S --needed --noconfirm zip unzip make \
  mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-llvm \
  mingw-w64-clang-x86_64-python mingw-w64-clang-x86_64-python-pip \
  mingw-w64-clang-x86_64-sdl3 mingw-w64-clang-x86_64-glslang || { echo "ERRO no pacman"; exit 1; }
python3 --version || { echo "ERRO: python3 nao encontrado"; exit 1; }
# the clang Python bindings (ptr32.py) match the installed clang; they load MSYS2's own libclang.dll
CLV=$(clang --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)
echo "clang $CLV"
python3 -c "import clang.cindex" 2>/dev/null || python3 -m pip install --break-system-packages "clang==$CLV.*" || { echo "ERRO ao instalar python clang"; exit 1; }
python3 -c "import clang.cindex as c; c.Index.create(); print('libclang ok')" || { echo "ERRO: libclang nao carregou"; exit 1; }
export BT3_CC=win64 BT3_SKELETON=1
mkdir -p port/build
echo "== 2/5 compilando o jogo (demora varios minutos)"
python3 port/tools/undefined.py > port/build/compile.txt 2>&1 || { tail -20 port/build/compile.txt; echo "ERRO ao compilar"; exit 1; }
if grep -q "^FAILED" port/build/compile.txt; then grep "^FAILED" port/build/compile.txt | head -20; echo "ERRO ao compilar"; exit 1; fi
tail -3 port/build/compile.txt
echo "== 3/5 linkando"
python3 port/tools/link.py || { echo "ERRO no link"; exit 1; }
echo "== 4/5 setup"
sh port/setup/build.sh win || { echo "ERRO no setup"; exit 1; }
echo "== 5/5 empacotando"
python3 port/tools/package.py || { echo "ERRO no package"; exit 1; }
ls -la port/build/*.zip
echo "PRONTO: port/build/Tenkaichi3Decomp-windows-x64.zip"
