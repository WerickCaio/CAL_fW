#ifndef DATABASE_H
#define DATABASE_H

#include <Arduino.h>

String DB_LoadMaster();
void DB_SaveMaster(String tag);
String DB_IdentifyUser(String tag); 
bool DB_AddUser(String tag);
bool DB_RemoveUser(String tag);
void DB_DumpToSerial();

// A NOSSA NOVA FUNÇÃO PARA O MONITOR SERIAL
bool DB_RenameUser(String tag, String novoNome); 

#endif