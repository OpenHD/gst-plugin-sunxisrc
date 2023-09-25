#include "jsmn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "jsonload.h"


static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}


static jsmntok_t *FindJsonElem(char *ElemName, jsmntok_t *Tokens, int NumTokens, const char *JsonBuf)
{
    for(int i = 0; i < NumTokens; i ++)
    {
        if(0 == jsoneq(JsonBuf, &Tokens[i], ElemName))
        {
            if(NumTokens > (i + 1))
            {
                return &Tokens[i + 1];
            }
            else
            {
                printf("No value available for %s, %d\n", ElemName, i + 1);
            }
        }
    }
    printf("Could not find config element %s\n", ElemName);
    return NULL;
}

bool LoadJsonConfig(char *Filename, JsonToConfigElem *ConfigElems, int NumConfigElems)
{
    jsmn_parser JsonParser;
    jsmntok_t *Tokens;
    char *JsonFileBuf = NULL;
    int NumTokens = sizeof(jsmntok_t) * NumConfigElems * 5;
    
    Tokens = malloc(NumTokens);
    if(!Tokens)
    {
        printf("Could not allocate memory for tokens\n");
        return false;
    }
    
    jsmn_init(&JsonParser);
    
     FILE *fp = fopen(Filename, "r");
    if(!fp)
    {
        printf("Could not open %s, exiting\n", Filename);
        return false;
    }
    
    fseek(fp, 0L, SEEK_END);
    int FileLen = ftell(fp);
    rewind(fp);
    
    JsonFileBuf = malloc(FileLen);
    if(!JsonFileBuf)
    {
        printf("Could not allocate %d bytes for Json file\n", FileLen);
    }
    int ReadLen = fread(JsonFileBuf, 1, FileLen, fp);
    fclose(fp);
    
    
    if(ReadLen != FileLen)
    {
        printf("Only read %d/%d bytes from json file\n", ReadLen, FileLen);
        return false;
    }
    
    NumTokens = jsmn_parse(&JsonParser, JsonFileBuf, ReadLen, Tokens, NumTokens);
    
    if (NumTokens < 0) {
        printf("Failed to parse JSON: %d\n", NumTokens);
        return false;
    }

    /* Assume the top-level element is an object */
    if (NumTokens < 1 || Tokens[0].type != JSMN_OBJECT) {
        printf("Object expected\n");
        return false;
    }
    
    printf("Loaded json file, now converting config\n");
    
    for (int i = 0; i < NumConfigElems; i++) 
    {
        jsmntok_t *JsonTok = FindJsonElem(ConfigElems[i].ElemName, Tokens, NumTokens, JsonFileBuf);
        if(JsonTok)
        {
            char *Val = JsonFileBuf + JsonTok->start;
            JsonFileBuf[JsonTok->end] = '\0';
            sscanf(Val, ConfigElems[i].ScanfChar, ConfigElems[i].Elem);
        }
    }
    
    free(Tokens);
    free(JsonFileBuf);
    
    printf("All done\n");
    return true;
}

   
    
