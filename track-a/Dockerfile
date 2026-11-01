FROM gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce AS build
WORKDIR /src
COPY main.cpp train.cpp ./
RUN g++ -O2 -std=c++20 -o /tmp/generate train.cpp && mkdir -p /tmp/pgodata /tmp/data /tmp/out && /tmp/generate /tmp/data/requests.log
RUN g++ -O3 -march=x86-64-v3 -std=c++20 -DNDEBUG -pthread -fprofile-generate=/tmp/pgodata -o /tmp/profiled main.cpp && \
    SD_DATA_DIR=/tmp/data SD_OUT_DIR=/tmp/out /tmp/profiled && \
    g++ -O3 -march=x86-64-v3 -std=c++20 -DNDEBUG -pthread -static -fprofile-use=/tmp/pgodata -fprofile-correction -s -o /tmp/profiled main.cpp && \
    cp /tmp/profiled /solution

FROM debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad
COPY --from=build /solution /solution
USER 65534:65534
ENTRYPOINT ["/solution"]
