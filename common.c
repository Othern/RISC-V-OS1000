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
    while(magnitude < num)
        magnitude *= 10;
    magnitude /= 10;
    while(num){
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