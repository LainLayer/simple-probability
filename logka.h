#ifndef LOGKA_H
#define LOGKA_H

// assume user included stdio.h

#define DEBUG_LABEL "[\033[36mDEBUG\033[0m/" /* Cyan   */
#define WARN_LABEL  "[\033[33m WARN\033[0m/" /* Yellow */
#define ERROR_LABEL "[\033[31mERROR\033[0m/" /* Red    */
#define INFO_LABEL  "[\033[34m INFO\033[0m/" /* Blue   */
#define OK_LABEL    "[\033[32m   OK\033[0m/" /* Green  */

#ifdef SILENT
	#define debug(format, ...) ((void)0)
	#define  warn(format, ...) ((void)0)
	#define error(format, ...) ((void)0)
	#define  info(format, ...) ((void)0)
	#define    ok(format, ...) ((void)0)
#else
	#define debug(format, ...) fprintf(stdout, DEBUG_LABEL "%s:%d]: " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
	#define  warn(format, ...) fprintf(stdout, WARN_LABEL  "%s:%d]: " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
	#define error(format, ...) fprintf(stdout, ERROR_LABEL "%s:%d]: " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
	#define  info(format, ...) fprintf(stdout, INFO_LABEL  "%s:%d]: " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
	#define    ok(format, ...) fprintf(stdout, OK_LABEL    "%s:%d]: " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#ifdef RELEASE
	#undef  debug
	#define debug(format, ...) ((void)0)
#endif

#endif // LOGKA_H
