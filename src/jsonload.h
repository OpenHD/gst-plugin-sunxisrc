#ifndef JSONLOAD_H
#define JSONLOAD_H


typedef struct
{
    char *ElemName;
    void *Elem;
    size_t ElemSize;
    char *ScanfChar;
} JsonToConfigElem;


bool LoadJsonConfig(char *Filename, JsonToConfigElem *ConfigElems, int NumConfigElems);

#endif
