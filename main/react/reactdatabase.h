// Autogenerated file
// Do not manually modify
// file:../main/react/reactdatabase.h



#ifndef	__REACTDATABASE_H_H
#define	__REACTDATABASE_H_H


#define	REACT_DATABASE_NBR	14
#define	REACT_DATAFILES_INFO_NBR	14


typedef struct _react_database{
	char *fname;
	uint8_t* data;
	size_t* size;
} react_database;


typedef struct _react_database_info{
	char     *fname;
	char     *path;
	char     *mime_type;
} react_database_info;
#endif


