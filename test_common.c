#include "test_common.h"
#include "common.h"

void test_common(){
    test_printf();
    test_memcpy();
    test_memset();
    test_strcpy();
    test_strcmp();
}

void test_printf() {
    printf("== printf ==\n");

    // 1. 純字串
    printf("plain string: hello world\n");

    // 2. %s
    printf("string: %s\n", "test");
    printf("empty string: '%s'\n", "");

    // 3. %d 基本
    printf("decimal 0: %d\n", 0);
    printf("decimal positive: %d\n", 123);
    printf("decimal negative: %d\n", -123);

    // 4. %d 邊界測試
    printf("decimal 9: %d\n", 9);
    printf("decimal 10: %d\n", 10);      
    printf("decimal 99: %d\n", 99);
    printf("decimal 100: %d\n", 100);    
    printf("decimal 1000: %d\n", 1000);  

    // 5. %x
    printf("hex 0: %x\n", 0);
    printf("hex 1: %x\n", 1);
    printf("hex 0x1234abcd: %x\n", 0x1234abcd);
    printf("hex 0xffffffff: %x\n", 0xffffffff);

    // 6. 混合格式
    printf("mix: s=%s d=%d x=%x\n", "abc", 42, 0x2a);

    // 7. 連續格式
    printf("%s %d %x\n", "hello", 7, 0xbeef);

    // 8. 未知格式
    printf("unknown: %q\n");

    // 9. 百分號後直接結尾
    printf("trailing percent: ");
    printf("%");
    printf("\n");

    // 10. 多個 %%
    printf("double unknown style: %%\n"); 
    // 注意：你目前不支援 %%，所以這行現在會輸出 "%%"
    // 若未來支援 %%，應該輸出 "%"
}

void test_memcpy(){
    printf("== memcpy ==\n");

    char src[] = "hello";
    char dst[10];

    memcpy(dst, src, 6);
    printf("copy: %s (expect hello)\n", dst);

    // n = 0
    char a[] = "abc";
    char b[] = "xxx";
    memcpy(b, a, 0);
    printf("n=0: %s (expect xxx)\n", b);

    // overlap (undefined behavior)
    char buf[] = "abcdef";
    memcpy(buf + 2, buf, 4);
    printf("overlap: %s (undefined)\n", buf);
}

void test_memset(){
    printf("== memset ==\n");

    char buf[10];

    memset(buf, 'A', 5);
    buf[5] = '\0';
    printf("fill: %s (expect AAAAA)\n", buf);

    memset(buf, 0, 5);
    printf("zero: %d %d %d (expect 0 0 0)\n", buf[0], buf[1], buf[2]);

    // n = 0
    char x[] = "test";
    memset(x, 'Z', 0);
    printf("n=0: %s (expect test)\n", x);
}

void test_strcpy(){
    printf("== strcpy ==\n");

    char src[] = "hello";
    char dst[10];

    strcpy(dst, src);
    printf("copy: %s (expect hello)\n", dst);

    // empty string
    char empty[] = "";
    strcpy(dst, empty);
    printf("empty: '%s' (expect '')\n", dst);

    // overwrite
    char big[] = "123456";
    strcpy(big, "abc");
    printf("overwrite: %s (expect abc)\n", big);
}

void test_strcmp(){
    printf("== strcmp ==\n");

    printf("equal: %d (expect 0)\n", strcmp("abc", "abc"));

    printf("less: %d (expect <0)\n", strcmp("abc", "abd"));

    printf("greater: %d (expect >0)\n", strcmp("abe", "abd"));

    printf("prefix1: %d (expect <0)\n", strcmp("abc", "abcd"));

    printf("prefix2: %d (expect >0)\n", strcmp("abcd", "abc"));
}