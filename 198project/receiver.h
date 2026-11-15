#ifndef RECEIVER_H
#define RECEIVER_H

namespace ModelResults {

bool busy();

bool timedOut();

bool beginWait();

void acceptResultsLine(const char *line);

} 
#endif 
