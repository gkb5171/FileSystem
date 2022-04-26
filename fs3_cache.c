////////////////////////////////////////////////////////////////////////////////
//
//  File           : fs3_cache.c
//  Description    : This is the implementation of the cache for the 
//                   FS3 filesystem interface.
//
//  Author         : Patrick McDaniel
//  Last Modified  : Sun 17 Oct 2021 09:36:52 AM EDT
//

// Includes
#include <cmpsc311_log.h>
#include "cmpsc311_log.h"
#include "fs3_controller.h"
#include <stdlib.h>
//#include <fs3_common.h>
// Project Includes
#include <fs3_cache.h>
#include <fs3_controller.h>
//#include <fs3_common.h>

//
// Support Macros/Data
typedef enum {T, F} boolean;
double Hits =0;
double Misses =0;
int Attempts =0;
double HitRatio;
int cacheSize;
int init=0;
int cacheCount = 0;
struct cacheParts{
    int sector;
    int track;
    int timeStamp;
    void *buffer;
    boolean used;
}*CACHE;




//
// Implementation

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_init_cache
// Description  : Initialize the cache with a fixed number of cache lines
//
// Inputs       : cachelines - the number of cache lines to include in cache
// Outputs      : 0 if successful, -1 if failure

int fs3_init_cache(uint16_t cachelines) {
    CACHE = (struct cacheParts *)malloc(cachelines * sizeof(struct cacheParts));
    if (cachelines > 0)
    {
        for(int i = 0; i<cachelines; i++){
        CACHE[i].sector=0;
        CACHE[i].track=0;
        CACHE[i].timeStamp=0;
        CACHE[i].buffer= (void *)malloc(FS3_SECTOR_SIZE);
        CACHE[i].used = F;
        }
    }    
    cacheSize = cachelines;
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_close_cache
// Description  : Close the cache, freeing any buffers held in it
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int fs3_close_cache(void)  {
    if (cacheSize>0){
        for(int i=0; i<cacheSize-1;i++){
        CACHE[i].sector=0;
        CACHE[i].track=0;
        CACHE[i].timeStamp=0;
        free(CACHE[i].buffer);
        CACHE[i].buffer=NULL;
        
        }
        free(CACHE);
    }
    
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_put_cache
// Description  : Put an element in the cache
//
// Inputs       : trk - the track number of the sector to put in cache
//                sct - the sector number of the sector to put in cache
// Outputs      : 0 if inserted, -1 if not inserted

int fs3_put_cache(FS3TrackIndex trk, FS3SectorIndex sct, void *buf) {
    int index;
    int full =1;
    if (cacheSize>0){       // make sure the size of the cache isn't 0
        if(cacheCount<cacheSize){cacheCount+=1;}    // increase number of items in cache until equal to cachelines
        // determine if cache is full //
        if(cacheCount<cacheSize){
            for(int i=0; i<cacheSize-1;i++){
                if(CACHE[i].used==F){
                    index = i;
                    CACHE[i].used = T;
                    full = 0;
                }
                if(full==0){break;}
            }
            CACHE[index].sector=sct;
            CACHE[index].track=trk;
            CACHE[index].timeStamp=Attempts;
            CACHE[index].buffer=buf;
        }
        // remove LRU //
        if(cacheCount == cacheSize){
            int oldest = CACHE[cacheSize-1].timeStamp;
            for(int j=0; j<cacheSize-1;j++){
                if(CACHE[j].timeStamp < oldest){
                    oldest = CACHE[j].timeStamp;
                    index = j;
                }
            }
            //free(CACHE[index].buffer);
            CACHE[index].buffer = buf;
            CACHE[index].sector=sct;
            CACHE[index].track = trk;
            CACHE[index].timeStamp=Attempts;
        }
        // assign values to cache //
        
    }
    
    //fs3_log_cache_metrics();
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_get_cache
// Description  : Get an element from the cache (
//
// Inputs       : trk - the track number of the sector to find
//                sct - the sector number of the sector to find
// Outputs      : returns NULL if not found or failed, pointer to buffer if found

void * fs3_get_cache(FS3TrackIndex trk, FS3SectorIndex sct)  {
    //int index;
    void *wanted;
    if(cacheSize>0){
        int inCache =0;
        Attempts+=1;
        
        //wanted = NULL;
        for(int i=0; i<cacheCount-1;i++){
            if((CACHE[i].sector==sct)&&(CACHE[i].track==trk)){
                Hits+=1;
                inCache=1;
                CACHE[i].timeStamp=Attempts;
                wanted = CACHE[i].buffer;
                //fs3_log_cache_metrics();
                //return(CACHE[i].buffer);
            }
            if(inCache==1){break;}
        }
        if(inCache==0){
            Misses+=1;
            wanted=NULL;
        //fs3_log_cache_metrics();
            //return(NULL);
        }
    }
    return(wanted);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_log_cache_metrics
// Description  : Log the metrics for the cache 
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int fs3_log_cache_metrics(void) {
    // calculate hit ratio //
    double atmp = Hits+Misses;
    HitRatio = Hits/atmp;
    HitRatio *= 100;
    logMessage(FS3DriverLLevel,"\nHits: %.0f\nMisses: %.0f\nAttemts: %d\nHit Ratio: %.2f percent",Hits, Misses, Attempts, HitRatio);
    return(0);
}
