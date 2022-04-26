////////////////////////////////////////////////////////////////////////////////
//
//  File           : fs3_driver.c
//  Description    : This is the implementation of the standardized IO functions
//                   for used to access the FS3 storage system.
//
//   Author        : Gregory Blickley
//   Last Modified : 10-19-2021
//

// Includes
#include <string.h>
#include "cmpsc311_log.h"
#include <sys/stat.h>
#include "fs3_controller.h"
#include <unistd.h>
#include <stdlib.h>
#include "fs3_cache.h"

// Project Includes
#include "fs3_driver.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
//	NOTE:																						  //
//			1) Add allocation functionallity												 	  //
//																								  //
////////////////////////////////////////////////////////////////////////////////////////////////////

// Defines
#define SECTOR_INDEX_NUMBER(x) ((int)(x/FS3_SECTOR_SIZE))
#define FS3_SIM_MAX_OPEN_FILES
//////////////////////////////////////////////////////////////////////////
//
// 						Static Global Variables
//
//////////////////////////////////////////////////////////////////////////

typedef enum {T, F} boolean;	// create an enum to allow use off boolean type
boolean diskIsMounted = F;		// see if disk is mounted

int trackSelect = 0;
int trackCounter = 1;
//int sectorSelect = 0;
int sectorCounter = 1;
void *calls;

//char *tempStoreBuf;
uint8_t *op;
uint16_t *sec;
uint32_t *trk;
uint8_t *ret;
boolean firstWrite = T;
boolean firstRead =T;
void *readBuffer;
FS3CmdBlk command;
FS3TrackIndex curTrk= 0;
FS3SectorIndex curSec= 0;
int fileCount = 0;

////////////////////////////////////////////////////////////////////////////////
//
//									create structs here
//
////////////////////////////////////////////////////////////////////////////////



struct fileParts{
	//void *fBuffer;
	char *path;
	int fileHandle;
	int position;
	int globalPos;
	int length;
	boolean isOpen;
	int sector;
	int track;
	boolean fileExisits;
}*FILES;

struct metaData{
	int trkLen; // number of tracks used for given file
	int secLen; // total number of sectors used for a given file
	int *trkArr; // stores the tracks used for a given file -> [Idx] returns track #
	int *secArr; // number of sectors per track for a given file [track] -> # of sectors on that track
	int **secAccess; // resizeable 2D array [track][sector] -> the sector from a given [trackIdx] and [sectorIdx] 	
}*META;


///////////////////////////////////////////////////////////////////////////
//
// 								Implementation
//
//////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_fileLocation
// Description  : finds the array index associated with the desired
//				  file directory/handle value
//
// Inputs       : file directory(fd)[from user]
// Outputs      : array index of fd

int fs3_fileLocation(int16_t fd){
	int curFile = -1;
	boolean foundFile =F;
	if(fileCount==1){curFile = 0;}
	else{
	for (int i =0; i<fileCount; i++){
		if(FILES[i].fileHandle==fd){
			curFile = i;
			foundFile =T;
			
		}
		if(foundFile==T){break;}
	}
	}
	logMessage(FS3DriverLLevel,"\n\nfile handle asked: %d\nFile handle given: %d\nFile array index: %d\nFile path: %s\nFile length: %d", fd, FILES[curFile].fileHandle, curFile, FILES[curFile].path, FILES[curFile].length);
	return (curFile);
}
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_track_index
// Description  : find the index of the given track for the given file
//				  
//
// Inputs       : curFile, track
// Outputs      : track index
int fs3_track_index(int curFile, int track){
	int trackLen = META[curFile].trkLen;
	int trkIdx;
	boolean foundT = F;
	for(int t=0; t<trackLen; t++){
		if(META[curFile].trkArr[t] == track){
			trkIdx = t;
			foundT = T;
		}
		if(foundT == T){break;}
	}
	return(trkIdx);
}
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_total_pos
// Description  : find the total position in a file
//				  
//
// Inputs       : curFile, sector, track
// Outputs      : total position in file
int fs3_sector_index(int curFile, int sector, int track){
	int curTrk = fs3_track_index(curFile, track);
	int secIdx;
	boolean foundS =F;

	if(META[curFile].secArr[curTrk]>1){
		for(int s=0; s<META[curFile].secArr[curTrk]-1; s++){
			if(META[curFile].secAccess[curTrk][s] == sector){
				secIdx =s;
				foundS = T;
			}
			if(foundS == T){break;}
		}
	}
	if(foundS == F){secIdx = 0;}

	return(secIdx);
}
////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_total_pos
// Description  : find the total position in a file
//				  
//
// Inputs       : curFile, sector, track
// Outputs      : total position in file
int fs3_total_pos(int curFile, int sector, int track){
	boolean foundS = F;
	int secIdx = fs3_sector_index(curFile, sector, track);
	
	//// ADD MULTI TRACK IMPLEMENTATION LATER  ////

	// 0) find index of current track in metaData
	int curTrk = fs3_track_index(curFile, track);
	
	// 1) find index of current sector in metaData
	/*
	if (META[curFile].secArr[curTrk]>1){
		for(int s=0; s<META[curFile].secArr[curTrk]-1; s++){
			if (META[curFile].secAccess[curTrk][s] == sector){
				secIdx = s;
				foundS = T;
			}
			if(foundS == T){break;}
		}
	}
	else{
		secIdx = 0;
	}
	*/
	//if(foundS == F){secIdx = 0;}
	// 2) multiple index by fs3_sector_size
	int sectorSpan = secIdx * FS3_SECTOR_SIZE;
	// 3) add current position
	int totalPos = sectorSpan + FILES[curFile].position;

	return(totalPos);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_find_sector
// Description  : Find the next free sector and add it to a files metaData
//				  
//
// Inputs       : curFile, track, sector
// Outputs      : new found sector
int fs3_find_sector(int curFile, int track, int sector){
	int newSec;// = sector+1;
	int checks=0;
	int trkIdx = fs3_track_index(curFile, track);
	boolean first = T;

	for(int i=sector; i<FS3_SECTOR_SIZE; i++){	// loop through sectors
		checks = -1;
		first =T;
	
		for(int f=0; f<fileCount; i++){		// loop through all the files

			for(int s=0; s<META[f].secArr[track]-1; s++){	// loop though the sectors on current track used
				if(META[f].secAccess[trkIdx][s] == i){	//checks to see if the sector is used by that file
					checks = 0;
				}
				if(checks == 0){	// break if sector is used
					break;
				}		
			}
			// end 
			if(checks == 0){	// break if sector is used -> move to next sector
				break;
			}

			if((checks != 0) && (checks != fileCount)){		// sector isn't used add a check
				if(first == T){checks=1;first=F;}
				else{checks+=1;}

			}
			if(checks == fileCount){
				newSec = i+1;
				break;}
			logMessage(FS3DriverLLevel,"checks: %d",checks);
		}
		// end
		if(checks == fileCount){break;}
		
	}
	//end
	if (checks==fileCount){		// assign the new sector value if all files give a check
			//newSec = i;
			META[curFile].secLen +=1;	// increase total sector len
			//META[curFile].secArr = realloc(META[curFile].secArr, (META[curFile].secArr[trkIdx] + 1) * sizeof(int));
			META[curFile].secArr[trkIdx]+=1;	// increase track sector len
			META[curFile].secAccess[trkIdx] = realloc(META[curFile].secAccess[trkIdx], META[curFile].secArr[trkIdx] * sizeof(int));
			META[curFile].secAccess[trkIdx][ META[curFile].secArr[trkIdx]-1 ] = newSec;	// assign sector to secAccess array
		}
	if(checks!= fileCount){newSec = -1;}	// if entire sector is ran though and sector isn't found return -1

	return(newSec);		// return the new secotr or -1 if none were found
}
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_find_track
// Description  : Find the next track and add it to the files metaData
//				  
//
// Inputs       : curFile, track
// Outputs      : new track
int fs3_find_track(int curFile, int track){
	int newtrack = track +1;
	int trkIdx = fs3_track_index(curFile, track)+1;

	META[curFile].trkLen+=1;	// increase total track length
	META[curFile].secArr=realloc(META[curFile].secArr, META[curFile].trkLen * sizeof(int));		//increase secArr length
	META[curFile].trkArr=realloc(META[curFile].trkArr, META[curFile].trkLen * sizeof(int));		//increase trkArr length
	META[curFile].secAccess=realloc(META[curFile].secAccess, META[curFile].trkLen * sizeof(int *));	// increse secAccess length
	META[curFile].secAccess[trkIdx]=realloc(META[curFile].secAccess[trkIdx], META[curFile].secArr[trkIdx] * sizeof(int));	// increse secAccess[trkIdx] length

	META[curFile].trkArr[trkIdx]=newtrack;
	return(newtrack);
}
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Function     : construct_fs3_cmdblock
// Description  : creates a commandblock which allows you to get the size of
//				  section (op,sector,track,etc)
//
// Inputs       : op-code, sector, track, return value
// Outputs      : pack 64-bit command block


// value = op << 60
// value = value | sec << 44
// value = value | trk << 12
// value = value | ret << 11

FS3CmdBlk construct_fs3_cmdblock(uint8_t op, uint16_t sec, uint_fast32_t trk, uint8_t ret){
	uint64_t get = 0x0, tempOp, tempSec, tempTrk, tempRet;

	tempOp = (uint64_t)op << 60;		// shift op code to left
	tempSec = (uint64_t)sec << 44;		// shift sec to right of op
	tempTrk = (uint64_t)trk << 12;		// shift trk to right of sec 
	tempRet = (uint64_t)ret << 11;		// shift ret to right of trk

	get = tempOp|tempSec|tempTrk|tempRet;	//pack the command block
	FS3CmdBlk returnGet = (FS3CmdBlk)get;	//get = (FS3CmdBlk)get;
	return(returnGet);
}
////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////
//
// Function     : deconstruct_fs3_cmdblock
// Description  : deconstructs the commandblock to asses values in the buff
//				  that are returned by the syscall
//
// Inputs       : *op-code, *sector, *track, *returnValue
// Outputs      : return value

int deconstruct_fs3_cmdblock(FS3CmdBlk cmdblock, uint8_t *op, uint16_t *sec, uint32_t *trk, uint8_t *ret){
	
	uint8_t tempOp = (cmdblock&0xff) >> 60;  // assign the op address to tempOp
	op = &tempOp;							// derefernce *op

	uint16_t tempSec = (cmdblock&0xffff) >> 44;		// assign the sec address to tempSec
	sec = &tempSec;									// derefernce *sec

	uint32_t tempTrk = (cmdblock&0xffffffff) >> 12;	// assign the trk address to tempTrk
	trk = &tempTrk;									// derefernce *trk

	uint8_t tempRet = (cmdblock&0xff) >> 11;		// assign the ret address to tempRet
	ret = &tempRet;									//// derefernce *ret
	return(0);							// return the return value
}


////////////////////////////////////////////////////////////////////////////////


//
////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_mount_disk
// Description  : FS3 interface, mount/initialize filesystem
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t fs3_mount_disk(void) {
	if (diskIsMounted == T){  // check if disk is already mounted (global variable)
		return(-1);
	}
	else{
		command = fs3_syscall(construct_fs3_cmdblock(0,0,0,0), calls);
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
		diskIsMounted = T;										// set diskIsMounted to TRUE
		
	}
	FILES = (struct fileParts *)malloc(fileCount * sizeof(struct fileParts));	// initialize FILES struct
	//META = (struct metaData *)malloc(fileCount * sizeof(struct metaData));	// initialize FILES struct
	return(0);
}

/////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_unmount_disk
// Description  : FS3 interface, unmount the disk, close all files
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t fs3_unmount_disk(void) {
	if (diskIsMounted == F){return(-1);}									// test to make sure the disk is mounted
	for (int i=0; i<fileCount-1; i++){												// loop through all the files
		if (FILES[i].isOpen == T){
		fs3_close(i+3);														// if file is open close it
		}														
	}
	free(FILES);
	command = fs3_syscall(construct_fs3_cmdblock(4,0,0,0),calls);				// call the unmount syscall
	deconstruct_fs3_cmdblock(command, op, sec, trk, ret);					// deconstruct the command block
	diskIsMounted = F;														// set diskIsMounted to false
	return(0);																// return 0 if successful

}

////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_open
// Description  : This function opens the file and returns a file handle
//
// Inputs       : path - filename of the file to open
// Outputs      : file handle if successful, -1 if failure

int16_t fs3_open(char *path) {
	int fh=0;

	logMessage(FS3DriverLLevel,"start open function");
	boolean fileExists = F;												// local variable to see if file exists
	

	
	if (fileCount > 0){
		for(int i =0; i<=fileCount-1; i++){
			if((FILES[i].path == path) && (FILES[i].isOpen == F)){
				fh = FILES[i].fileHandle;
				FILES[i].isOpen = T;
				FILES[i].position = 0;
				FILES[i].globalPos=0;
				FILES[i].track = i+1;
				FILES[i].sector=0;
				fileExists = T;
			}
			else if((FILES[i].path == path) && (FILES[i].isOpen == T)){return(-1);}
			if(fileExists == T){break;}
		}
	}	
	
	if (fileExists == F){											// if file doesn't already exist
		fileCount +=1;
		int fileIdx = fileCount -1;
		FILES = realloc(FILES, fileCount*sizeof(struct fileParts));	// increse size of FILES struct
		META = realloc(META, fileCount*sizeof(struct metaData)); // increase size of META struct
		fh = fileCount+2;
		FILES[fileIdx].path = path;
		FILES[fileIdx].length = 0;
		FILES[fileIdx].position = 0;
		FILES[fileIdx].globalPos = 0;
		FILES[fileIdx].isOpen = T;
		FILES[fileIdx].fileHandle = fh;
		
		META[fileIdx].secLen=1;	// increase total sector length
		META[fileIdx].trkLen=1;	// increase track length
		
		META[fileIdx].secArr = (int *)malloc(sizeof(int));	// allocate space for secArr 
		META[fileIdx].secArr[0] = 0;		// assign the first sector as sector 0 to the first track 
		
		META[fileIdx].trkArr = (int *)malloc(sizeof(int));     // allocate space for trkArr
		META[fileIdx].trkArr[0]=0;			// assign the first track as track 0

		META[fileIdx].secAccess = (int **)malloc(sizeof(int *));		// allocate space for 2d array
		if(META[fileIdx].secAccess){
			for(int i=0; i<META[fileIdx].trkLen; i++){
				META[fileIdx].secAccess[i] = (int *)malloc(sizeof(int));
			}
		}
		META[fileIdx].secAccess[0][0]= fileIdx;		// assign first sector

		FILES[fileIdx].sector = META[fileIdx].secAccess[0][0]; // set the current sector
		FILES[fileIdx].track = META[fileIdx].trkArr[0];		// set the current track
		fileExists = T;
	}
	
	
	logMessage(FS3DriverLLevel,"file handle given: %d",fh);// FILES.fileHandle[x]);
	return(fh); // if it hits here it fails so i guess -1
}


////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_close
// Description  : This function closes the file
//
// Inputs       : fd - the file descriptor
// Outputs      : 0 if successful, -1 if failure




int16_t fs3_close(int16_t fd) {
	
	
	boolean fileExists =F;										// determine if the file exists
	int fileIdx = fileCount -1;
	for (int i = 0; i<fileIdx; i++){
		if (FILES[i].fileHandle == fd){							// validate the file handle
			fileExists = T;
			if (FILES[i].isOpen==F){return(-1);}				// fail if the file is NOT open

			else if (FILES[i].isOpen==T){
			FILES[i].isOpen = F;								// set the file to closed
			logMessage(FS3DriverLLevel, "this is %s close", FILES[i].path);
			FILES[i].position =0;								// set the file position to 0
			FILES[i].sector =0;
			return(0);
			}
		}
	}
	if (fileExists == F){return(-1);}							// fail if file does not exist
	
	
	return(-1);			// if it gets here it fails so i guess -1
}

	

////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_read
// Description  : Reads "count" bytes from the file handle "fh" into the 
//                buffer "buf"
//
// Inputs       : fd - filename of the file to read from
//                buf - pointer to buffer to read into
//                count - number of bytes to read
// Outputs      : bytes read if successful, -1 if failure

int32_t fs3_read(int16_t fd, void *buf, int32_t count) {	 

	   ////     Files Tests     ////
	int curFile = fs3_fileLocation(fd);
	if(curFile == -1){return(-1);}
	if(FILES[curFile].isOpen==F){return(-1);}
	  
	  ////	   call seek syscall    ////
	curTrk = FILES[curFile].track;
	curSec = FILES[curFile].sector;
	readBuffer = fs3_get_cache(curTrk, curSec);
	if(readBuffer == NULL){
		readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
		fs3_put_cache(curTrk,curSec,readBuffer); 
	}


	command = fs3_syscall(construct_fs3_cmdblock(1, 0, FILES[curFile].track, 0), readBuffer);	// call the strk syscall
	deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
	
	////	Requires only 1 sector     ////

	if ((FS3_SECTOR_SIZE - FILES[curFile].position)>=count){			// test to see if count < the space between position and end of sec

				////    create buffers    ////
		
		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}
		command = fs3_syscall(construct_fs3_cmdblock(2, FILES[curFile].sector,0,0), readBuffer);			// perform read
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
		
		char *localBuf0 = (char *)malloc(FS3_SECTOR_SIZE);		// create char buf to store read data

		memcpy(localBuf0, readBuffer, (FS3_SECTOR_SIZE));			// copy read data into char buffer
		char *newBuf = (char *)malloc((count));	// create a buffer the size of count

		int staticPos= FILES[curFile].position;		// set var for loop
		int counter =0;

		for(int pos = staticPos; pos<staticPos+count; pos++){				// collect wanted data
			newBuf[counter]=localBuf0[pos];						// read localBuf0 and write to newBuf
			
				counter+=1;
			
		}
		free(localBuf0);
		localBuf0 = NULL;
		memcpy(buf, newBuf, (count));//*sizeof(char)); //copy read data into buf
		free(newBuf);		// free newBuf
		newBuf=NULL;
			//  update position  //
		int fPos = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track) + count;
		
		fs3_seek(fd, fPos);
		logMessage(FS3DriverLLevel, "end of single read");
		
	}	

	////	Multiple Sectors    ////

	else{
		logMessage(FS3DriverLLevel,"begin multi sector read");
					////    create counts     ////
		int count1 = FS3_SECTOR_SIZE - FILES[curFile].position;		//first count
		int count2 = (count- count1)% FS3_SECTOR_SIZE;				// last count	
		int loopCount = (count-count1)/FS3_SECTOR_SIZE;				// number of sector counts
		int bufTracker =0;											// keep track of location inside merge buf

					////    create buffers    ////
		char *mergeBuf=(char *)malloc((count));		// allocate space of count*sizeof(char) to hold all read data
		
		
					////   start of count1 read    ////
		
		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}

		command = fs3_syscall(construct_fs3_cmdblock(2,FILES[curFile].sector, 0, 0), readBuffer);		// perform read
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);		

					// create local buffer and copy data //
		char *localBuf = (char *)malloc(FS3_SECTOR_SIZE);		// allocate char buffer for read data

		memcpy(localBuf, readBuffer, FS3_SECTOR_SIZE);					// copy read data over to char buffer
					//    copy wanted data into merge   //
		int staticPos = FILES[curFile].position;
		for(int pos = staticPos; pos<staticPos+count1;pos++){
			mergeBuf[bufTracker] = localBuf[pos];
			if(pos<staticPos+count1){
			bufTracker+=1;
			}
		}
		free(localBuf);		// free localBuf
		localBuf=NULL;
					//    update file position    //
		int fPos = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track) + count1;
		//FILES[curFile].position=0;
		//FILES[curFile].sector+=1;
		fs3_seek(fd, fPos);
				////     Loop for Multi read     ////
		if(loopCount>0){
			logMessage(FS3DriverLLevel, "start loop for multi read");
			for(int i=1; i<=loopCount;i++){				// loop for number of whole sectors
				
				curTrk = FILES[curFile].track;
				curSec = FILES[curFile].sector;
				readBuffer = fs3_get_cache(curTrk, curSec);
				if(readBuffer == NULL){
					readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
					fs3_put_cache(curTrk,curSec,readBuffer); 
				}

				command = fs3_syscall(construct_fs3_cmdblock(2, FILES[curFile].sector, 0,0),readBuffer);
				deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
				char *tempLoc = (char *)malloc(FS3_SECTOR_SIZE);			// allocate space to store read data

				memcpy(tempLoc, readBuffer, FS3_SECTOR_SIZE);	//copy read data into char buffer
				for(int pos=0; pos<FS3_SECTOR_SIZE;pos++){
					mergeBuf[bufTracker] = tempLoc[pos];
					
						bufTracker+=1;
					
				}
				fPos = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track) + FS3_SECTOR_SIZE;
				//FILES[curFile].sector+=1;
				fs3_seek(fd, fPos);
				free(tempLoc);
				tempLoc = NULL;
			}
							// fill in later if needed  //
		}

				//// start of count2 read   ////
		logMessage(FS3DriverLLevel,"begin count2 read");

		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}
		command = fs3_syscall(construct_fs3_cmdblock(2, FILES[curFile].sector, 0,0), readBuffer);		// perform read
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);

		logMessage(FS3DriverLLevel,"before localBuf2");
		char *localBuf2 = (char *)malloc(FS3_SECTOR_SIZE);		// allocate char buffer to store read data
		logMessage(FS3DriverLLevel,"before memcpy");
		memcpy(localBuf2, readBuffer, FS3_SECTOR_SIZE);						// copy read data over to char buffer
				
				// copy wanted data into merge  //
		staticPos = FILES[curFile].position;
		logMessage(FS3DriverLLevel,"before loop");
		for(int pos = staticPos ; pos<staticPos + count2;pos++){
			mergeBuf[bufTracker] = localBuf2[pos];
			if(pos<staticPos+count2){
			bufTracker+=1;
			}
		}
		logMessage(FS3DriverLLevel,"before free");
		free(localBuf2);	// free localBuf2
		localBuf2 = NULL;
		logMessage(FS3DriverLLevel,"before log");
		
			//  update file position  //
		fPos = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track) + count2;
		fs3_seek(fd, fPos);
		//FILES[curFile].position+=count2;
			//// 	Ending Process for Multi read    ////
		logMessage(FS3DriverLLevel,"before last memcpy");
		memcpy(buf, mergeBuf, (count));//*sizeof(char));//*sizeof(char));							//copy all read data into buffer
		logMessage(FS3DriverLLevel,"sector Quantity: %d ,data read:\n%s",FILES[curFile].sector,buf);
		free(mergeBuf);			// free mergeBuf
		mergeBuf = NULL;
		
		logMessage(FS3DriverLLevel,"end of multi read");
		return(count);
	}

	////	return     ////
	logMessage(FS3DriverLLevel,"value returned: %d", count);
	return(count);
	
}
	 
	
	
	
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_write
// Description  : Writes "count" bytes to the file handle "fd" from the 
//                buffer  "buf"
//
// Inputs       : fd - filename of the file to write to
//                buf - pointer to buffer to write from
//                count - number of bytes to write
// Outputs      : bytes written if successful, -1 if failure

int32_t fs3_write(int16_t fd, void *buf, int32_t count) {
	logMessage(FS3DriverLLevel, "called write function");

		////    Files Tests    ////
	int curFile = fs3_fileLocation(fd);
	if(curFile == -1){return(-1);}
	if(FILES[curFile].isOpen!=T){return(-1);}
	int totalPosition = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track);
	logMessage(FS3DriverLLevel,"current length: %d, total position: %d, count: %d", FILES[curFile].length, totalPosition, count);
	
		//// INCREASE FILE LENGTH ////
	if ((FILES[curFile].length-totalPosition)<count)
	{
		FILES[curFile].length+=count - (FILES[curFile].length-totalPosition);
		logMessage(FS3DriverLLevel, "length added: %d", (FILES[curFile].length-totalPosition));
		int totalSpace = META[curFile].secLen * FS3_SECTOR_SIZE;
		logMessage(FS3DriverLLevel, "length: %d\ntotal space: %d", FILES[curFile].length, totalSpace);
		if(FILES[curFile].length > totalSpace){
			int foundSec = fs3_find_sector(curFile, FILES[curFile].track, FILES[curFile].sector);
			if (foundSec == -1){
				logMessage(FS3DriverLLevel, "no sector was found exiting.");
				exit(0);	// exit because we haven't implemented adding a new track yet
			}
		}
	}
	logMessage(FS3DriverLLevel,"length after increase: %d", FILES[curFile].length);
		////    Call Seek syscall    ////
	
	curTrk = FILES[curFile].track;
	curSec = FILES[curFile].sector;
	readBuffer = fs3_get_cache(curTrk, curSec);
	if(readBuffer == NULL){
		readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
		fs3_put_cache(curTrk,curSec,readBuffer); 
	}

	command = fs3_syscall(construct_fs3_cmdblock(1, 0, FILES[curFile].track, 0), readBuffer);
	deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
	

		////    Single Sector Write    ////
	if ((FILES[curFile].position+count)<=FS3_SECTOR_SIZE){
		logMessage(FS3DriverLLevel, "begin single sector write");


			// create and assign inputBuf  //
		char *inputBuf=(char *)malloc((count)); 
		memcpy(inputBuf, buf, (count));//*sizeof(char));


			//  perform read  //
		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}
		command = fs3_syscall(construct_fs3_cmdblock(2, FILES[curFile].sector, 0, 0), readBuffer);
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
	
		char *tempBuf = (char *)malloc(FS3_SECTOR_SIZE);

			//  assign secBuf  //
		memcpy(tempBuf, readBuffer, FS3_SECTOR_SIZE);

			//  loop to write data into buffer  //
		int staticPos = FILES[curFile].position;
		int Counter =0;
		for(int pos = staticPos; pos<staticPos+count; pos++){
			tempBuf[pos] = inputBuf[Counter];
			
				Counter+=1;
			
		}
		free(inputBuf);
		inputBuf = NULL;
			//  copy altered data in wrBuf  //
		memcpy(readBuffer, tempBuf, FS3_SECTOR_SIZE);
		logMessage(FS3DriverLLevel,"Data written: %s", readBuffer);
		free(tempBuf);
			//  perform write syscall  //
		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}
		
		command = fs3_syscall(construct_fs3_cmdblock(3, FILES[curFile].sector, 0, 0), readBuffer);
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
		
			//  update file position  //
		int fPos = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track) + count;
		fs3_seek(fd, fPos);
		//FILES[curFile].position+=count;
		logMessage(FS3DriverLLevel,"\n\nfile position: %d\n file sector: %d\n count: %d", FILES[curFile].position, FILES[curFile].sector,count);
		
	}
	

		////    Multi Sector Write    ////							////////////////			ERROR WITH MEMORY VALGRIND   SAYS LINES :717->729  & 655->729
	else{
		logMessage(FS3DriverLLevel,"begin multi sector write");

			//  create counts  //
		int count1 = FS3_SECTOR_SIZE - FILES[curFile].position;
		int count2 = (count - count1) % FS3_SECTOR_SIZE;
		int loopCount = (count - count1)/FS3_SECTOR_SIZE;
		int bufTracker = 0;
			//  allocate buffer space  //
		char *inputBuf = (char *)malloc(count);
			//  assign inputBuf data  //
		memcpy(inputBuf, buf, (count));

			//  perform first read  //
		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}
		command = fs3_syscall(construct_fs3_cmdblock(2, FILES[curFile].sector, 0, 0), readBuffer);
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
	
			// create tempBuf  //
		char *tempBuf = (char *)malloc(FS3_SECTOR_SIZE);
			//  assign secBuf data  //
		memcpy(tempBuf, readBuffer, FS3_SECTOR_SIZE);
		
			//  write data into secBuf  //

		int staticPos = FILES[curFile].position;
		
		
		for(int pos = staticPos; pos<staticPos+count1; pos++){
			tempBuf[pos] = inputBuf[bufTracker];
			
				bufTracker+=1;
			
		}
			//  assign wrBuf new data  //
		memcpy(readBuffer, tempBuf, FS3_SECTOR_SIZE);
		free(tempBuf);
		tempBuf = NULL;
			//  perform the first write  //
		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}
		command = fs3_syscall(construct_fs3_cmdblock(3, FILES[curFile].sector, 0, 0), readBuffer);
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
			//  update file position  //
		int fPos = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track) + count1;
		fs3_seek(fd, fPos);
		//FILES[curFile].position=0;
		//FILES[curFile].sector+=1;
		logMessage(FS3DriverLLevel,"\n\nfile position: %d\n file sector: %d\n count: %d", FILES[curFile].position, FILES[curFile].sector,count1);


			////    BEGIN LOOP FOR MULTI SECTOR WRITE    ////
		if(loopCount>0){
		
					
			for(int i=1; i<=loopCount; i++){
				
				curTrk = FILES[curFile].track;
				curSec = FILES[curFile].sector;
				readBuffer = fs3_get_cache(curTrk, curSec);
				if(readBuffer == NULL){
					readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
					fs3_put_cache(curTrk,curSec,readBuffer); 
				}
				command = fs3_syscall(construct_fs3_cmdblock(2, FILES[curFile].sector, 0, 0), readBuffer);
				deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
		
				char *tempLoc = (char *)malloc(FS3_SECTOR_SIZE);
				memcpy(tempLoc, readBuffer,FS3_SECTOR_SIZE);
				staticPos = FILES[curFile].position;
				for(int pos=staticPos; pos<FS3_SECTOR_SIZE; pos++){
					tempLoc[pos] = inputBuf[bufTracker];
					
						bufTracker+=1;
					
				}
				memcpy(readBuffer, tempLoc, FS3_SECTOR_SIZE);
				free(tempLoc);
				tempLoc = NULL;
				
				curTrk = FILES[curFile].track;
				curSec = FILES[curFile].sector;
				readBuffer = fs3_get_cache(curTrk, curSec);
				if(readBuffer == NULL){
					readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
					fs3_put_cache(curTrk,curSec,readBuffer); 
				}
				command = fs3_syscall(construct_fs3_cmdblock(3, FILES[curFile].sector, 0,0),readBuffer);
				deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
				
				fPos = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track) + FS3_SECTOR_SIZE;
				fs3_seek(fd, fPos);
				//FILES[curFile].sector+=1;
				logMessage(FS3DriverLLevel,"\n\nfile position: %d\n file sector: %d\n count: %d", FILES[curFile].position, FILES[curFile].sector,count1);

			}

		}


			////    START FINAL WRITE    ////
		
		

			// perform read (Final) //
		
		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}	
		command = fs3_syscall(construct_fs3_cmdblock(2, FILES[curFile].sector, 0, 0), readBuffer);
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);
		tempBuf = (char *)malloc(FS3_SECTOR_SIZE);
		
			//  assign data to secBuf (Final)  //
		memcpy(tempBuf,readBuffer, FS3_SECTOR_SIZE);

			//  write data into secBuf  //
		staticPos = FILES[curFile].position;
		logMessage(FS3DriverLLevel, "start loop");
		for(int pos = staticPos; pos<staticPos+count2;pos++){
			tempBuf[pos] = inputBuf[bufTracker];			
			
				bufTracker+=1;
			
		}
		free(inputBuf);
		inputBuf = NULL;
			//  copy new data into wrBuf  //
		memcpy(readBuffer, tempBuf, FS3_SECTOR_SIZE);
		free(tempBuf);
		tempBuf = NULL;

			//  perform last write  //
		
		curTrk = FILES[curFile].track;
		curSec = FILES[curFile].sector;
		readBuffer = fs3_get_cache(curTrk, curSec);
		if(readBuffer == NULL){
			readBuffer = (void *)malloc(FS3_SECTOR_SIZE);
			fs3_put_cache(curTrk,curSec,readBuffer); 
		}
		command = fs3_syscall(construct_fs3_cmdblock(3,FILES[curFile].sector,0, 0), readBuffer);
		deconstruct_fs3_cmdblock(command, op, sec, trk, ret);

		fPos = fs3_total_pos(curFile, FILES[curFile].sector, FILES[curFile].track) + count2;
		fs3_seek(fd, fPos);
		//FILES[curFile].position+=count2;
	
		logMessage(FS3DriverLLevel,"\n\nfile position: %d\n file sector: %d\n count: %d", FILES[curFile].position, FILES[curFile].sector,count1);

		
		logMessage(FS3DriverLLevel, "end of multi sector write");
		
	
	}	
	
	int stop = 0;
	if(stop == 0){stop =1;}
	return(count);
	
}
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_seek
// Description  : Seek to specific point in the file
//
// Inputs       : fd - filename of the file to write to
//                loc - offfset of file in relation to beginning of file
// Outputs      : 0 if successful, -1 if failure



int32_t fs3_seek(int16_t fd, uint32_t loc) {

	/////   NOT FULLY IMPLEMENTED FOR MULTIPLE TRACKS    //////
	int curFile;											// create current file variable

	int newPos = loc % FS3_SECTOR_SIZE;						// gets the remainder of the location wanted divided by sector size to determine position in given sector
	int newSec = loc / FS3_SECTOR_SIZE;						// gets the quoteint of the loc wanted divided by sector size 
	int secIdx = newSec;
	
	curFile = fs3_fileLocation(fd);
	if (curFile != -1){										// if file does exist
		if (FILES[curFile].length < loc){return(-1);}		// if loc is OUT of range for the file fail

		if (FILES[curFile].isOpen != T){return(-1);}		// if file is not open fail

		
		else if (FILES[curFile].length >= loc){				// if loc is IN range of the file
		//int secIdx = fs3_sector_index(curFile, newSec, FILES[curFile].track);
		 
		FILES[curFile].position = newPos; //loc % FS3_SECTOR_SIZE;						// set the position of the file equal to loc
		FILES[curFile].sector = META[curFile].secAccess[0][secIdx]; //loc / FS3_SECTOR_SIZE;								// assign the sector needed
		FILES[curFile].track = FILES[curFile].track;
		return(0);											// return 0
		}
	}
	else if (curFile == -1){return(-1);}					// if file does NOT exist fail
	
	
	return(-1);												// if it gets here something went wrong so i guess return -1


}


////////////////////////////////////////////////////////////////