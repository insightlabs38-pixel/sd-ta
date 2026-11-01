FROM python:3.13-slim@sha256:d98cc29d3266e535dd9da1099688d9cb3ca4753813571e071763d17725460962 AS training
WORKDIR /train
COPY a_ingest.py .
RUN python3 a_ingest.py --seed 17 --scale sample --out /train/data

FROM gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce AS build
WORKDIR /src
COPY main.cpp .
COPY --from=training /train/data/requests.log /src/requests.log
RUN mkdir -p /src/pgo /src/out && \
    cp main.cpp /tmp/original.cpp && \
    sed -i 's/_exit(0); \/\/ the kernel/std::exit(0); \/\/ the kernel/' main.cpp && \
    g++ -O3 -march=x86-64-v3 -std=c++20 -DNDEBUG -pthread -fprofile-generate=/src/pgo -static-libstdc++ -static-libgcc -o solution main.cpp && \
    SD_DATA_DIR=/src SD_OUT_DIR=/src/out SD_THREADS=12 /src/solution && \
    find /src/pgo -name '*.gcda' | grep -q . && \
    mv /tmp/original.cpp main.cpp && \
    g++ -O3 -march=x86-64-v3 -std=c++20 -DNDEBUG -pthread -fprofile-use=/src/pgo -fprofile-correction -Wno-error=coverage-mismatch -static-libstdc++ -static-libgcc -s -o solution main.cpp && \
    cp solution /solution

FROM debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad
COPY --from=build /solution /solution
USER 65534:65534
ENTRYPOINT ["/solution"]
