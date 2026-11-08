#ifndef MKSC_LUA_USER_H
#define MKSC_LUA_USER_H
/* Included by lua.h through LUA_USER_H. Upstream seeds string hashing from
 * time and addresses, and table.sort's pivot from clock(); mod scripts must
 * behave identically on every run, so both are constants here. */
#define luai_makeseed(L) (0x4d4b5343u + 0u * (unsigned int)sizeof(L))
#define l_randomizePivot() 0x9e3779b9u
#endif
