#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

static unsigned long long value(const char *path) {
  FILE *f=fopen(path,"r"); assert(f); unsigned long long n;
  assert(fscanf(f,"%llu",&n)==1); fclose(f); return n;
}
static unsigned long long blocks(int fd) {
  struct stat st; assert(!fstat(fd,&st)); return (unsigned long long)st.st_blocks*512;
}
static void stage(const char *name,int fd) {
  printf("STAGE %s blocks=%llu current=%llu peak=%llu\n",name,blocks(fd),
         value("/sys/fs/cgroup/memory.current"),value("/sys/fs/cgroup/memory.peak"));
}
static void zero_range(int fd,off_t from,size_t n) {
  unsigned char buf[4096];
  while(n) {size_t k=n<sizeof buf?n:sizeof buf;assert(pread(fd,buf,k,from)==(ssize_t)k);
    for(size_t i=0;i<k;++i)assert(buf[i]==0); from+=k;n-=k;}
}
int main(void) {
  const size_t page=(size_t)sysconf(_SC_PAGESIZE), n=32u<<20;
  int fd=open("/out/summary.txt",O_RDWR|O_CREAT|O_TRUNC,0644);assert(fd>=0);
  struct statfs fs;assert(!fstatfs(fd,&fs));assert(fs.f_type==TMPFS_MAGIC);
  assert(!ftruncate(fd,n));stage("sparse",fd);assert(blocks(fd)==0);
  unsigned char *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);assert(p!=MAP_FAILED);
  assert(!madvise(p,n,MADV_NOHUGEPAGE));memset(p,0x5a,n);stage("filled",fd);
  unsigned long long full=blocks(fd);assert(full==n);
  assert(!munmap(p,n));stage("unmapped",fd);assert(blocks(fd)==full);
  p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);assert(p!=MAP_FAILED);
  assert(!madvise(p,n,MADV_DONTNEED));stage("dontneed",fd);assert(blocks(fd)==full);
  errno=0;assert(madvise(p+1,page,MADV_REMOVE)==-1 && errno==EINVAL);
  assert(blocks(fd)==full);stage("unaligned-remove-rejected",fd);
  assert(!fallocate(fd,FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,1,page-2));
  unsigned char edge;assert(pread(fd,&edge,1,0)==1&&edge==0x5a);
  assert(pread(fd,&edge,1,page-1)==1&&edge==0x5a);zero_range(fd,1,page-2);
  assert(blocks(fd)==full);stage("partial-page-retained",fd);
  assert(!fallocate(fd,FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,page,2*page));
  zero_range(fd,page,2*page);assert(blocks(fd)==full-2*page);stage("aligned-punch",fd);
  assert(!madvise(p+4*page,2*page,MADV_REMOVE));zero_range(fd,4*page,2*page);
  assert(blocks(fd)==full-4*page);stage("aligned-remove",fd);
  assert(!fallocate(fd,FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,0,n));
  assert(blocks(fd)==0);stage("all-backing-released",fd);
  assert(!munmap(p,n));close(fd);return 0;
}
