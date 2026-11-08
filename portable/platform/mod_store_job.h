#ifndef PORTABLE_MOD_STORE_JOB_H
#define PORTABLE_MOD_STORE_JOB_H
#include "mod_store.h"
typedef struct PortableStoreJob PortableStoreJob;
/* Worker borrows store until free; the UI must not mutate it while busy. */
PortableStoreJob *portable_store_job_load(const char *url,const char *cache,PortableStore *store);
PortableStoreJob *portable_store_job_download(const PortableStore *store,unsigned entry,const char *destination);
/* 0 running, 1 success, -1 failure. */
int portable_store_job_poll(PortableStoreJob *job,char *error);
void portable_store_job_free(PortableStoreJob *job);
#endif
