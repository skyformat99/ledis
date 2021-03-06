#include "sds.h"

/*
 *  返回新的buf值（并不返回sdshdr整个对象）
 */
char *sdsnew(const char *init){
    if(init != NULL){
        return sdsnewlen(init, strlen(init));
    }else{
        return sdsnewlen(init, 0);
    }
}

char *sdsnewlen(const char *init, size_t initlen){

    struct sdshdr *sh;

    if(init){
        //初始化有值就用malloc
        //结构体大小+存储的字符串大小+空白结尾
        //printf("init is not null");
        sh = malloc((sizeof(struct sdshdr)) + initlen + 1);
    }else{
        //printf("init is null");
        //初始化没值就用calloc
        sh = calloc(1, (sizeof(struct sdshdr)) + initlen + 1);
    }

    //分配失败就直接返回
    if(sh == NULL){
        return NULL;
    }

    //设置初始长度和剩余空间（不留）
    sh->len = initlen;
    sh->free = 0;

    //如果init有内容，则复制到buf字段里
    if(init && initlen){
        memcpy(sh->buf, init, initlen);
    }

    //统一追加结束符，不管init有没有
    sh->buf[initlen] = '\0';

    return (char*)sh->buf;
}

/*
 *  创建一个只有空字符串的sds
 */
char *sdsempty(void){
    return sdsnewlen("", 0);
}

void sdsfree(char *buf){
    if(buf == NULL){
        return;
    }
    free(buf - sizeof(struct sdshdr));
}

size_t sdslen(const char *buf){
    //传入的是里面字符串的指针，可以通过减去结构体本身长度，返回字符串所属结构体的指针
    struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));
    return sh->len;
}

size_t sdsavail(const char *buf){
    //传入的是里面字符串的指针，可以通过减去结构体本身长度，返回字符串所属结构体的指针
    struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));
    return sh->free;
}

char *sdsdup(const char *buf){
    return sdsnewlen(buf, strlen(buf));
}

void sdsclear(char *buf){
    //惰性清空，只是修改free和len的值，然后第一位增加结束符即可，不清空每个字节
    struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));
    sh->free = (sh->free - sh->len);
    sh->len = 0;
    sh->buf[0] = '\0';
}

char *sdscat(char *buf, const char *t){
    return sdscatlen(buf, t, strlen(t));
}
char *sdscatlen(char *buf, const char *t, size_t len){
    struct sdshdr *sh;

    //暂存旧的len值
    size_t oldlen = sdslen(buf);

    buf = sdsMakeRoom(buf, len);

    if(buf == NULL){
        return NULL;
    }

    //这里sh已经是新的了，free值也是扩展后的
    sh = (void*)(buf - (sizeof(struct sdshdr)));

    //复制新字符串到最后
    memcpy(buf + oldlen, t, len);
    sh->len = oldlen + len;
    sh->free = sh->free - len;
    sh->buf[oldlen + len] = '\0';

    return buf;
}

char *sdscpy(char *buf, const char *t){
    return sdscpylen(buf, t, strlen(t));
}
char *sdscpylen(char *buf, const char *t, size_t len){
   struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));

   //获取原来的总长度
   size_t totallen = (sh->free + sh->len);

   //总长度都没有新内容大，则需要扩展
   if(totallen < len){
       buf = sdsMakeRoom(buf, (len-sh->len));
       if(buf == NULL){
           return NULL;
       }
       //用新的buf获取新的sh，算出新的总长度
       sh = (void*)(buf - (sizeof(struct sdshdr)));
       totallen = (sh->free + sh->len);
   }

   //复制内容，直接覆盖
   memcpy(buf, t, len);

   //sdsMakeRoom只是返回了buf，并没有重设len和free字段，最后还要单独设置
   buf[len] = '\0';
   sh->len = len;
   sh->free = totallen - len;
   return buf;
}

/*
 * 对比2个字符串，参数已经是char*了，那就和直接调用memcmp没有区别了应该
 * buf1 > buf2 返回1
 * buf1 < buf2 返回-1
 * buf1 == buf2 返回0
 */
int sdscmp(const char *buf1, const char *buf2){
    size_t len1, len2, minlen;
    len1 = sdslen(buf1);
    len2 = sdslen(buf2);
    minlen = (len1 < len2) ? len1 : len2;
    return memcmp(buf1, buf2, minlen);
}

/*
 * 去掉前后cset出现过的字符
 */
char *sdstrim(char *buf, const char *cset){
    struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;    //前两个不变，后两个会变
    
    sp = buf;
    start = buf;
    ep = buf + sdslen(buf) - 1; //不要最后那个\0
    end = buf + sdslen(buf) - 1;

    //修剪完只有sp和ep的指向发生了变化
    while(sp <= end && strchr(cset, *sp)){
        sp++;
    }
    while(ep > start && strchr(cset, *ep)){
        ep--;
    }

    size_t len = (sp > ep) ? 0 : ((ep - sp) + 1);

    //如果sp变了，则将剩余的字符串整体前移
    if(sh->buf != sp){
        memmove(sh->buf, sp, len);
    }

    //更新属性
    sh->len = len;
    sh->free = sh->free + (sh->len - len);
    sh->buf[len] = '\0';

    return sh->buf;
}

/*
 * 截取字符串其中的一段
 * start和end都是索引从0开始，并且包含自身
 * 索引可以为负数
 * 直接修改buf自身
 */
void sdsrange(char *buf, int start, int end){
    struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));
    size_t len = sdslen(buf);
    if(len == 0){
        return;
    }

    if(start < 0){
        //如果是负数就倒着计算
        start = len + start;
        if(start < 0){
            start = 0;
        }
    }

    if(end < 0){
        end = len + end;
        if(end < 0){
            end = 0;
        }
    }

    size_t newlen = (start > end) ? 0 : (end - start + 1);
    if(newlen != 0){
        if(start >= len){   //start超出原始长度，newlen自然为0
            newlen = 0;
        }else if(end >= len){   //end超出原始长度，当做最后一个字符，再重新计算newlen
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    }else{
        start = 0;
    }

    if(start && newlen){    //2个参数都不为0，开始移动字符串，用start和newlan来移动，不需要end了
        memmove(sh->buf, sh->buf+start, newlen);
    }

    sh->buf[newlen] = '\0';    //源代码写的是0，这里暂时改写成\0
    sh->free = sh->free + (sh->len - newlen);
    sh->len = newlen;
    //不用再返回了
}

/*
 * 将buf里面的字符串全部变成小写
 */
void sdstolower(char *buf){
    int len = sdslen(buf);
    for (int i=0; i < len; i++){
        buf[i] = tolower(buf[i]);
    }
}

/*
 * 将buf里面的字符串全部变成大写
 */
void sdstoupper(char *buf){
    int len = sdslen(buf);
    for (int i=0; i < len; i++){
        buf[i] = toupper(buf[i]);
    }
}

/*
 * 返回sds全部已分配的内存字节数
 * 1.指针本身的长度
 * 2.字符串本身长度（len）
 * 3.剩余的长度（free）
 * 4.结束符长度（1）
 */
size_t sdsAllocSize(char *buf){
    struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));
    return (sizeof(*sh)) + sh->len + sh->free + 1;
}

char *sdsMakeRoom(char *buf, size_t addlen){
    
    struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));
    struct sdshdr *newsh;
    size_t free = sdsavail(buf);
    size_t len = sdslen(buf);
    size_t newlen = (len + addlen); //合并后正好的长度

    //如果剩余空间大于新的长度，则直接返回不扩展
    if(free >= addlen){
        return buf;
    }

    //最终长度如果小于1M，直接翻倍扩容，否则最多只增加1M空间
    if(newlen < SDS_MAX_PREALLOC){
        newlen = newlen * 2;
    }else{
        newlen = newlen + SDS_MAX_PREALLOC;
    }

    newsh = realloc(sh, sizeof(struct sdshdr) + newlen + 1);
    if(newsh == NULL){
        return NULL;
    }

    //更新空余长度
    newsh->free = newlen - len;
    return newsh->buf;
}

char *sdsRemoveFreeSpace(char *buf){
    struct sdshdr *sh = (void*)(buf - (sizeof(struct sdshdr)));
    sh = realloc(sh, sizeof(struct sdshdr) + sh->len + 1);
    sh->free = 0;
}

int main(){
    //char* v1 = sdsnew("hello");
    //size_t len1 = sdslen(v1);
    //printf("%lu", len1);

    /* const char *ch = "hello";
    char *ch2 = "world";
    ch = ch2;
    printf("%s", ch); */

    
    
    getchar();
    return 0;
}