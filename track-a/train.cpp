// Synthetic non-scoring profile workload, generated during Docker build.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  FILE *f = fopen(argv[1], "wb");
  if (!f) return 2;
  std::vector<std::string> eps;
  eps.reserve(1842);
  for (int i=0;i<1842;++i) {
    eps.push_back("/api/v2/resource" + std::to_string(i) + "/{id}");
    if (i%20==0) eps.back() += "/projects/{project_id}/actions";
  }
  eps[0] = "/";
  eps[1] = "/health";
  eps[2] = "/v2/orders";
  eps[3] = "/v2/orders/{id}";
  std::mt19937 rng(20261101);
  std::uniform_real_distribution<double> uni(0.,1.);
  for (int i=0;i<620000;++i) {
    int h=i/8612;
    if (h>=72) h=71;
    if (i%1307==0 && h>0) --h;
    int day=(h+18)/24, hh=(h+18)%24;
    int month= day<2 ? 10 : 11;
    int d = day==0 ? 30 : day==1 ? 31 : day==2 ? 1 : 2;
    int id=std::min(1841,int(std::exp(uni(rng)*std::log(1842.))));
    uint32_t lat=(rng()%150000)+1;
    if (i%9907==0) lat=0;
    if (i%11993==0) lat=UINT32_MAX;
    int status= i%97==0 ? 500 : (i%109==0 ? 599 : (i%191==0 ? 499 : 200));
    const char *method=i%19==0 ? "OPTIONS" : i%3==0 ? "POST" : "GET";
    fprintf(f,"2026-%02d-%02dT%02d:%02d:%02d.%06dZ %s %s %03d %u\n",
      month,d,hh,(i/61)%60,i%60,i%1000000,method,eps[id].c_str(),status,lat);
  }
  fclose(f);
  return 0;
}
