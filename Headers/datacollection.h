/*
 * datacollection.h
 *
 *  Created on: Jun 26, 2012
 *      Author: alpsayin
 */

#ifndef DATACOLLECTION_H_
#define DATACOLLECTION_H_

#define LOGFILE_NAME "radiotftp-event.log"

void openLogFile(char* logfile_name);
void registerEvent(char* event, char* param);
void closeLogFile();


#endif /* DATACOLLECTION_H_ */
