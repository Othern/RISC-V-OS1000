#include "common.h"

void putchar(char ch);

static inline void printString(const char* s){
    while (*s) {
        putchar(*s);
        s++;
    }
}

static inline void printPointer(unsigned int p){
    for(int i=7; i>=0; i--){
        unsigned int tmp = (p >> (i*4)) & 0xf;
        if(tmp >= 10){
            putchar('a' + tmp -10);
        }else{
            putchar('0' + tmp);
        }
    }
}

static inline void printNum(int num){
    if(num == 0){
        putchar('0');
        return;
    }
    if(num < 0) {
        putchar('-');
        num = -num;
    }
    int magnitude = 1;
    int count  = 1;
    while(magnitude <= num / 10){
        magnitude *= 10;
        count++;
    }
    while(count--){
        putchar('0' + num /magnitude);
        num %= magnitude;
        magnitude /= 10;
    }
}

void printf(const char* fmt, ...){
    va_list vargs;
    va_start(vargs, fmt);

    while(*fmt != '\0'){
        if(*fmt == '%'){
            fmt++;
            switch (*fmt) {
                case '\0':

                    goto end;
                case 's':{
                    const char *s = va_arg(vargs, const char *);
                    printString(s);
                    break;
                }
                case  'd':{
                    int num = va_arg(vargs, int);
                    printNum(num);
                    break;
                }
                case  'x':{
                    unsigned int x = va_arg(vargs,unsigned int);
                    printPointer(x);
                    break;
                }
                default:
                    putchar('%');
                    putchar(*fmt);
                    break;

            }
        }        
        else putchar(*fmt);

        fmt++;
    }
end:
    va_end(vargs);

}

void *memcpy(void *dst, const void *src, size_t n){
    uint8_t *d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while(n--){
        *d++ = *s++;
    }
    return dst;
}

void *memset(void* buf, char c, size_t n){
    uint8_t *d = (uint8_t*)buf;
    while(n--){
        *d++ = c;
    }
    return buf;
}

char *strcpy(char *dst, const char *src) {
    char* d = dst;
    while(*src){
        *d++ = *src++;
    }
    *d = '\0';
    return dst;
}

int strcmp(const char *s1,const char *s2){
    while(*s1 && *s2){
        if(*s1 != *s2) break;
        s1++;
        s2++; 
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}