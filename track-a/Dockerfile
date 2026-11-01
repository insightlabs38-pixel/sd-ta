FROM gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce AS build
WORKDIR /src
COPY main.cpp .
# Train GCC's branch and block layout on deterministic, generated legal logs.
# The instrumented build uses exit() so libgcov can flush its profile.
RUN mkdir -p /tmp/pgo/data /tmp/pgo/out /tmp/pgo-prof && \
    awk 'BEGIN { srand(20261101); for (i=0; i<1500000; i++) { \
      h=int(i*72/1500000); if(i%101==0 && h>0) h--; \
      mo=(h<30?10:11); day=(h<6?30:h<30?31:h<54?1:2); hh=(h+18)%24; \
      e=int(exp(rand()*log(1842))); \
      ep=(e%47==0?"/internal/v1/tenants/{tenant_id}/projects/{project_id}/pipelines/{pipeline_id}/runs/{run_id}/artifacts/{artifact_id}:download":sprintf("/api/v2/items/%04d/{id}",e)); \
      status=(i%101==0?599:i%97==0?499:200); \
      lat=(i%503==0?4294967295:int(rand()*1000000)); \
      printf("2026-%02d-%02dT%02d:%02d:%02d.%06dZ GET %s %d %.0f\n",mo,day,hh,i%60,int(i/60)%60,i%1000000,ep,status,lat); \
    }}' > /tmp/pgo/data/requests.log && \
    g++ -O3 -march=x86-64-v3 -std=c++20 -DNDEBUG -pthread \
      -fprofile-generate=/tmp/pgo-prof -D_exit=exit -o /tmp/solution main.cpp && \
    SD_THREADS=6 SD_DATA_DIR=/tmp/pgo/data SD_OUT_DIR=/tmp/pgo/out /tmp/solution && \
    g++ -O3 -march=x86-64-v3 -std=c++20 -DNDEBUG -pthread \
      -fprofile-use=/tmp/pgo-prof -fprofile-correction -static -s \
      -o /tmp/solution main.cpp && \
    cp /tmp/solution /solution

FROM debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad
COPY --from=build /solution /solution
USER 65534:65534
ENTRYPOINT ["/solution"]
