/*
** Purpose: Implement the coreJSON adapter
**
** Notes:
**   1. This provides a wrapper for apps to use coreJSON for their tables.
**
** References:
**   1. OpenSatKit Object-based Application Developer's Guide.
**   2. cFS Application Developer's Guide.
**
**   Written by David McComas, licensed under the Apache License, Version 2.0
**   (the "License"); you may not use this file except in compliance with the
**   License. You may obtain a copy of the License at
**
**      http://www.apache.org/licenses/LICENSE-2.0
**
**   Unless required by applicable law or agreed to in writing, software
**   distributed under the License is distributed on an "AS IS" BASIS,
**   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**   See the License for the specific language governing permissions and
**   limitations under the License.
*/

/*
** Include Files:
*/

#include <string.h>
#include "cjson.h"


/***********************/
/** Macro Definitions **/
/***********************/

#define PRINT_BUF_SEGMENT_BYTES 100   /* Number of bytes in each OS_printf() call */ 


/**********************/
/** Type Definitions **/
/**********************/

typedef enum
{

   OBJ_OPTIONAL,
   OBJ_REQUIRED

} OBJ_Necessity_t;


/************************************/
/** Local File Function Prototypes **/
/************************************/

static boolean LoadObj(CJSON_Obj_t* Obj, const char* Buf, size_t BufLen, OBJ_Necessity_t Necessity);

static void PrintJsonBuf(const char* JsonBuf, size_t BufLen);
static boolean ProcessFile(const char* Filename, char* JsonBuf, size_t MaxJsonFileChar,
                           CJSON_LoadJsonData_t LoadJsonData,
                           CJSON_LoadJsonDataAlt_t LoadJsonDataAlt, void* UserDataPtr,
                           boolean CallbackWithUserData);

static boolean StubLoadJsonData(size_t JsonFileLen);
static boolean StubLoadJsonDataAlt(size_t JsonFileLen, void* UserDataPtr);


/**********************/
/** Global File Data **/
/**********************/

/* JSONStatus_t - String lookup table */

static const char* JsonStatusStr[] = {
  
  "ValidButPartial",    /* JSONPartial          */
  "Valid",              /* JSONSuccess          */
  "Invalid-Malformed",  /* JSONIllegalDocument  */
  "MaxDepthExceeded",   /* JSONMaxDepthExceeded */
  "QueryKeyNotFound",   /* JSONNotFound         */
  "QueryNullPointer",   /* JSONNullParameter    */
  "QueryKeyInvalid",    /* JSONBadParameter     */
  
};

/* JSONTypes_t -  String lookup table */

static const char* JsonTypeStr[] = {
  
  "Invalid",  /* JSONInvalid */
  "String",   /* JSONString  */
  "Number",   /* JSONNumber  */
  "True",     /* JSONTrue    */
  "False",    /* JSONFalse   */
  "Null",     /* JSONNull    */
  "Object",   /* JSONObject  */
  "Array",    /* JSONArray   */
  
};


/******************************************************************************
** Function: CJSON_ObjConstructor
**
** Notes:
**    1. This is used to construct individual CJSON_Obj_t structures. This 
**       constructor is not needed if the user creates a static CJSON_Obj_t
**       array with default values.
*/
void CJSON_ObjConstructor(CJSON_Obj_t* Obj, const char* QueryKey, 
                          JSONTypes_t JsonType, void* TblData, size_t TblDataLen)
{

   Obj->Updated    = FALSE;
   Obj->TblData    = TblData;
   Obj->TblDataLen = TblDataLen;   
   Obj->Type       = JsonType;
   
   if (strlen(QueryKey) <= CJSON_MAX_KEY_LEN) {
      
      strncpy (Obj->Query.Key, QueryKey, CJSON_MAX_KEY_LEN);
      Obj->Query.KeyLen = strlen(Obj->Query.Key);
   }
   else {
   
      CFE_EVS_SendEvent(CJSON_OBJ_CONSTRUCT_ERR_EID, CFE_EVS_ERROR,
                        "Error constructing table. Query key %s exceeds maximum key length %d.",
                        QueryKey, CJSON_MAX_KEY_LEN);
   
   }
      
} /* End CJSON_ObjConstructor() */


/******************************************************************************
** Function: CJSON_LoadObjArray
**
** Notes:
**    1. See CJSON_LoadObj() for supported JSON types
**
*/
size_t CJSON_LoadObjArray(CJSON_Obj_t* Obj, size_t ObjCnt, char* Buf, size_t BufLen)
{
   
   int     i;
   size_t  ObjLoadCnt = 0;
   
   for (i=0; i < ObjCnt; i++) {
   
      if (CJSON_LoadObj(&Obj[i], Buf, BufLen)) ObjLoadCnt++;
      
   } /* End object loop */
   
   return ObjLoadCnt;
      
} /* End CJSON_LoadObjArray() */


/******************************************************************************
** Function: CJSON_ProcessFile
**
** Notes:
**  1. See ProcessFile() for details.
**  2. The JsonBuf pointer is passed in as an unused UserDataPtr. 
*/
boolean CJSON_ProcessFile(const char* Filename, char* JsonBuf, 
                          size_t MaxJsonFileChar, CJSON_LoadJsonData_t LoadJsonData)
{


   return ProcessFile(Filename, JsonBuf, MaxJsonFileChar, LoadJsonData, StubLoadJsonDataAlt, (void*)JsonBuf, FALSE);

   
} /* End CJSON_ProcessFile() */


/******************************************************************************
** Function: CJSON_ProcessFile
**
** Notes:
**  1. See ProcessFile() for details.
*/
boolean CJSON_ProcessFileAlt(const char* Filename, char* JsonBuf, 
                             size_t MaxJsonFileChar, CJSON_LoadJsonDataAlt_t LoadJsonDataAlt,
                             void* UserDataPtr)
{

   return ProcessFile(Filename, JsonBuf, MaxJsonFileChar, StubLoadJsonData, LoadJsonDataAlt, UserDataPtr, TRUE);

   
} /* End CJSON_ProcessFileAlt() */


/******************************************************************************
** Function: CJSON_LoadObj
**
** Notes:
**    1. See LoadObj()'s switch statement for supported JSON types
**
*/
boolean CJSON_LoadObj(CJSON_Obj_t* Obj, const char* Buf, size_t BufLen)
{
   
   return LoadObj(Obj, Buf, BufLen, OBJ_REQUIRED);
   
} /* End CJSON_LoadObj() */


/******************************************************************************
** Function: CJSON_LoadObjOptional
**
** Notes:
**    1. See LoadObj()'s switch statement for supported JSON types
**
*/
boolean CJSON_LoadObjOptional(CJSON_Obj_t* Obj, const char* Buf, size_t BufLen)
{
   
   return LoadObj(Obj, Buf, BufLen, OBJ_OPTIONAL);
   
} /* End CJSON_LoadObjOptional() */


/******************************************************************************
** Function: CJSON_ObjTypeStr
**
** Type checking should enforce valid parameter but check just to be safe.
*/
const char* CJSON_ObjTypeStr(JSONTypes_t  ObjType)
{

   uint8 i = 0;
   
   if ( ObjType >= JSONInvalid &&
        ObjType <= JSONArray) {
   
      i =  ObjType;
   
   }
        
   return JsonTypeStr[i];

} /* End CJSON_ObjTypeStr() */


/******************************************************************************
** Function: LoadObj
**
** Notes:
**    None
**
*/
static boolean LoadObj(CJSON_Obj_t* Obj, const char* Buf, size_t BufLen, OBJ_Necessity_t Necessity)
{
   
   boolean      RetStatus = FALSE;
   JSONStatus_t JsonStatus;
   const char   *Value;
   size_t       ValueLen;
   JSONTypes_t  ValueType;
   char         *ErrCheck;
   char         NumberBuf[20], StrBuf[256];
   int          IntValue;
   
   Obj->Updated = FALSE;
      
   JsonStatus = JSON_SearchConst(Buf, BufLen, 
                                 Obj->Query.Key, Obj->Query.KeyLen,
                                 &Value, &ValueLen, &ValueType);
                                 
   if (JsonStatus == JSONSuccess) { 
   
      CFE_EVS_SendEvent(CJSON_LOAD_OBJ_EID, CFE_EVS_DEBUG,
                        "CJSON_LoadObj: Type=%s, Value=%s, Len=%ld",
                        JsonTypeStr[ValueType], Value, ValueLen);

      switch (ValueType) {
         
         case JSONString:
         
            if (ValueLen <= Obj->TblDataLen) {
            

               strncpy(StrBuf,Value,ValueLen);
               StrBuf[ValueLen] = '\0';
               
               memcpy(Obj->TblData,StrBuf,ValueLen+1);
               Obj->Updated = TRUE;
               RetStatus = TRUE;
            
            }
            else {
               
               CFE_EVS_SendEvent(CJSON_LOAD_OBJ_ERR_EID, CFE_EVS_ERROR, "JSON string length %ld exceeds %s's max length %ld", 
                                 ValueLen, Obj->Query.Key, Obj->TblDataLen);
            
            }
            break;
   
         case JSONNumber:
         
            strncpy(NumberBuf,Value,ValueLen);
            NumberBuf[ValueLen] = '\0';
            IntValue = (int)strtol(NumberBuf, &ErrCheck, 10);
			   if (ErrCheck != NumberBuf) {
               memcpy(Obj->TblData,&IntValue,sizeof(int));
               Obj->Updated = TRUE;
               RetStatus = TRUE;
            }
            else {
               CFE_EVS_SendEvent(CJSON_LOAD_OBJ_ERR_EID, CFE_EVS_ERROR,
                                 "CJSON number conversion error for value %s",
                                 NumberBuf);
            }
            
            break;

         case JSONArray:
         
            CFE_EVS_SendEvent(CJSON_LOAD_OBJ_EID, CFE_EVS_INFORMATION,
                              "JSON array %s, len = %ld", Value, ValueLen);
            PrintJsonBuf(Value, ValueLen);
         
            break;

         case JSONObject:
         
            CFE_EVS_SendEvent(CJSON_LOAD_OBJ_EID, CFE_EVS_INFORMATION,
                              "JSON array %s, len = %ld", Value, ValueLen);
            PrintJsonBuf(Value, ValueLen);
         
            break;

         default:
         
            CFE_EVS_SendEvent(CJSON_LOAD_OBJ_ERR_EID, CFE_EVS_ERROR,
                              "Unsupported JSON type %s returned for query %s", 
                              JsonTypeStr[ValueType], Obj->Query.Key);
      
      } /* End ValueType switch */
      
   }/* End if successful search */
   else {
   
      if (Necessity == OBJ_REQUIRED)
      {
         CFE_EVS_SendEvent(CJSON_LOAD_OBJ_EID, CFE_EVS_INFORMATION,
                           "JSON search error for query %s. Status = %s.", 
                           Obj->Query.Key, JsonStatusStr[JsonStatus]);
      }
   }
         
   return RetStatus;
   
} /* End LoadObj() */


/******************************************************************************
** Function: PrintJsonBuf
**
** Notes:
**    1. OS_printf() limits the number of characters so this loops through
**       printing 100 bytes per OS_printf() call
**
*/
static void PrintJsonBuf(const char* JsonBuf, size_t BufLen)
{
   
   int  i = 0;
   char PrintBuf[PRINT_BUF_SEGMENT_BYTES+1];
   
   OS_printf("\n>>> JSON table file buffer:\n");
   for (i=0; i < BufLen; i += PRINT_BUF_SEGMENT_BYTES)
   {
      
      strncpy(PrintBuf, &JsonBuf[i], PRINT_BUF_SEGMENT_BYTES);
      PrintBuf[PRINT_BUF_SEGMENT_BYTES] = '\0';
      OS_printf("%s",PrintBuf); 
      
   }
   OS_printf("\n");
   
} /* End PrintJsonBuf() */


/******************************************************************************
** Function: ProcessFile
**
** Notes:
**  1. Entire JSON file is read into memory
**  2. User callback function LoadJsonData() or LoadJsonDataAlt() calls 
**     a CJSON_LoadObj*() method to load the JSON data into the user's table. 
**     The user's callback function can perform table-specific procesing such  
**     as validation prior to loading the table data.
**  3. The alternate callback method allows the user to pass in a pointer to
**     their JSON file processing data structure which is then passed back to
**     the callback function. This is needed in situations when the caller
**     needs to be reentrant and doesn't own the JSON file procesing data
**     structure. 
**
*/
static boolean ProcessFile(const char* Filename, char* JsonBuf, size_t MaxJsonFileChar,
                           CJSON_LoadJsonData_t LoadJsonData,
                           CJSON_LoadJsonDataAlt_t LoadJsonDataAlt, void* UserDataPtr,
                           boolean CallbackWithUserData)
{

   int          FileHandle;
   int32        ReadStatus;
   JSONStatus_t JsonStatus;

   boolean  RetStatus = FALSE;
   
   FileHandle = OS_open(Filename, OS_READ_ONLY, 0);
   
   /*
   ** Read entire JSON table into buffer. Logic kept very simple and JSON
   ** validate will catch if entire file wasn't read.
   */
   if (FileHandle >= 0) {

      ReadStatus = OS_read(FileHandle, JsonBuf, MaxJsonFileChar);

      if (ReadStatus == OS_FS_ERROR) {

         CFE_EVS_SendEvent(CJSON_PROCESS_FILE_ERR_EID, CFE_EVS_ERROR, 
                           "CJSON error reading file %s. Status = 0x%08X",
                           Filename, ReadStatus);

      }
      else
      {
         
         if (DBG_JSON) PrintJsonBuf(JsonBuf, ReadStatus);
         
         /* ReadStatus equals buffer len */

         JsonStatus = JSON_Validate(JsonBuf, ReadStatus);

         if (JsonStatus == JSONSuccess)
         { 
            if (CallbackWithUserData)
            {
               RetStatus = LoadJsonDataAlt(ReadStatus,UserDataPtr);
            }
            else
            {
               RetStatus = LoadJsonData(ReadStatus);
            }
         }
         else
         {
         
            CFE_EVS_SendEvent(CJSON_PROCESS_FILE_ERR_EID, CFE_EVS_ERROR, 
                              "CJSON error validating file %s.  Status = %s.",
                              Filename, JsonStatusStr[JsonStatus]);

         }
         
      } /* End if valid read */
   }/* End if valid open */
   else {

      CFE_EVS_SendEvent(CJSON_PROCESS_FILE_ERR_EID, CFE_EVS_ERROR, "CJSON error opening file %s. Status = 0x%08X", 
                        Filename, FileHandle);
      
   }
   
   return RetStatus;
   
} /* End ProcessFile() */


/******************************************************************************
** Function: StubLoadJsonData
**
** Notes:
**   1. This serves as an unused stub parameter in calls to ProcessFile()
**      therefore it should never get executed.
**
*/
static boolean StubLoadJsonData(size_t JsonFileLen)
{
   
   CFE_EVS_SendEvent(CJSON_INTERNAL_ERR_EID, CFE_EVS_CRITICAL, 
      "StubLoadJsonData() called, JsonFileLen %ld. Code structural error that requires a developer",
      JsonFileLen);
      
   return FALSE;

} /* End StubLoadJsonData() */


/******************************************************************************
** Function: StubLoadJsonDataAlt
**
** Notes:
**   1. This serves as an unused stub parameter in calls to ProcessFile()
**      therefore it should never get executed.
**
*/
static boolean StubLoadJsonDataAlt(size_t JsonFileLen, void* UserDataPtr)
{
   
   CFE_EVS_SendEvent(CJSON_INTERNAL_ERR_EID, CFE_EVS_CRITICAL, 
      "StubLoadJsonDataAlt() called, JsonFileLen %ld, UserDataPtr 0x%p. Code structural error that requires a developer",
      JsonFileLen, UserDataPtr);

   return FALSE;

} /* End StubLoadJsonDataAlt() */