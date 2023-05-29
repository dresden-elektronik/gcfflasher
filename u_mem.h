#ifndef U_MEM_H
#define U_MEM_H

void U_memset(void *mem, int c, unsigned long n);
void U_bzero(void *mem, unsigned long n);

void *U_memcpy(void *dst, const void *src, unsigned long n);

#endif /* U_MEM_H */
