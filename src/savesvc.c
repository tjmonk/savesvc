/*==============================================================================
MIT License

Copyright (c) 2023 Trevor Monk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
==============================================================================*/

/*!
 * @defgroup savesvc Save Service
 * @brief Variable Server Save Service
 * @{
 */

/*============================================================================*/
/*!
@file savesvc.c

    Variable Save Service

    The Variable Save Service will write out all modified non-volatile
    variables into an output file which is compatible with the loadconfig
    utility.

    Variables are written out when the save service is triggered by the
    trigger variable specified in the command line arguments

    Variables are written to the output file specified in the command
    line arguments

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <limits.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <varserver/varserver.h>
#include <varserver/varquery.h>

/*==============================================================================
       Type Definitions
==============================================================================*/
typedef struct _savesvcState
{
    /*! handle to the variable server */
    VARSERVER_HANDLE hVarServer;

    /*! output file name */
    char *filename;

    /*! trigger variable name */
    char *triggervar;

    /*! handle to the trigger variable */
    VAR_HANDLE hTriggerVar;

    /*! verbose output flag */
    bool verbose;

    /*! output file descriptor */
    int fd;

    /*! temporary output file name */
    char tmpfile[BUFSIZ];

} SaveSvcState;

/*==============================================================================
       Function declarations
==============================================================================*/
static void usage( char *cmdname );
static void SetupTerminationHandler( void );
static void TerminationHandler( int signum, siginfo_t *info, void *ptr );
static int ProcessOptions( int argC,
                           char *argV[],
                           SaveSvcState *pState );
static int RunSvc( SaveSvcState *pState );
static int InitConfig( SaveSvcState *pState );
static int WriteConfig( SaveSvcState *pState );
static int WriteConfigVars( SaveSvcState *pState );
static int FinalizeConfig( SaveSvcState *pState );

/*==============================================================================
       Definitions
==============================================================================*/

/*! default output filename */
#define DEFAULT_OUTPUT_FILENAME "/tmp/usersettings.cfg"

/*! default trigger variable */
#define DEFAULT_TRIGGER_VARIABLE "/sys/config/save"

/*==============================================================================
      File Scoped Variables
==============================================================================*/

/*! pointer to the Save Service State */
SaveSvcState *pState;

/*==============================================================================
       Function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the vars application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
int main(int argC, char *argV[])
{
    VAR_HANDLE hVar;
    int rc;

    pState = NULL;

    /* create the vars utility instance */
    pState = (SaveSvcState *)calloc(1, sizeof( SaveSvcState ) );
    if ( pState != NULL )
    {
        /* set the default output filename */
        pState->filename = DEFAULT_OUTPUT_FILENAME;

        /* set the default trigger variable */
        pState->triggervar = DEFAULT_TRIGGER_VARIABLE;

        /* clear the file descriptor */
        pState->fd = -1;

        /* get a handle to the variable server for transition events */
        pState->hVarServer = VARSERVER_Open();
        if ( pState->hVarServer != NULL )
        {
            /* set up the termination handler to clean up if
               the server quits unexpectedly */
            SetupTerminationHandler();

            /* Process Options */
            ProcessOptions( argC, argV, pState );

            if ( pState->triggervar != NULL )
            {
                /* get a handle to the trigger variable */
                hVar = VAR_FindByName( pState->hVarServer, pState->triggervar );
                if ( hVar != VAR_INVALID )
                {
                    pState->hTriggerVar = hVar;

                    /* request MODIFIED notification from the varserver
                       for the trigger variable */
                    rc = VAR_Notify( pState->hVarServer,
                                     pState->hTriggerVar,
                                     NOTIFY_MODIFIED );
                    if ( rc == EOK )
                    {
                        /* run the service */
                        RunSvc( pState );
                    }
                    else
                    {
                        fprintf( stderr,
                                 "notification request failed for %s\n",
                                 pState->triggervar );
                    }
                }
                else
                {
                    fprintf( stderr,
                             "Cannot find trigger variable: %s\n",
                             pState->triggervar );
                }
            }
            else
            {
                fprintf( stderr, "No trigger variable specified\n");
            }

            /* close the variable server */
            VARSERVER_Close( pState->hVarServer );
            pState->hVarServer = NULL;

        }
        else
        {
            fprintf( stderr, "Cannot open variable server\n" );
        }

        free( pState );
    }

    return 0;
}

/*============================================================================*/
/*  usage                                                                     */
/*!
    Display the vars utility usage

    The usage function dumps the application usage message
    to stderr.

    @param[in]
       cmdname
            pointer to the invoked command name

    @return none

==============================================================================*/
static void usage( char *cmdname )
{
    if( cmdname != NULL )
    {
        fprintf(stderr,
                "usage: %s [-f name] [-t varname] [-v] [-h]\n"
                " [-f filename] : output file name\n"
                " [-t triggervar] : trigger variable name\n"
                " [-h] : display this help\n"
                " [-v] : verbose output\n",
                cmdname );
    }
}

/*============================================================================*/
/*  ProcessOptions                                                            */
/*!
    Process the command line options

    The ProcessOptions function processes the command line options and
    populates the iotsend state object

    @param[in]
        argC
            number of arguments
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @param[in]
        pState
            pointer to the vars state

    @return none

==============================================================================*/
static int ProcessOptions( int argC,
                           char *argV[],
                           SaveSvcState *pState )
{
    int c;
    const char *options = "hvt:f:";

    if( ( pState != NULL ) &&
        ( argV != NULL ) )
    {
        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'v':
                    pState->verbose = true;
                    break;

                case 't':
                    pState->triggervar = optarg;
                    break;

                case 'f':
                    pState->filename = optarg;
                    break;

                case 'h':
                    usage( argV[0] );
                    break;

                default:
                    break;

            }
        }
    }

    return 0;
}

/*============================================================================*/
/*  RunSvc                                                                    */
/*!
    Run the save service

    The RunSvc function waits for a MODIFIED signal on the trigger
    variable and writes out the configuration file containing all of
    the dirty variables

    Under normal circumstances this function will not return

    @param[in]
        pState
            pointer to the SaveSvc state

    @retval EOK - success
    @retval EINVAL - invalid arguments

==============================================================================*/
static int RunSvc( SaveSvcState *pState )
{
    int result = EINVAL;
    int sig;
    int fd;
    int sigval;

    if ( pState != NULL )
    {
        result = EOK;

        /* set up the signal file descriptor to receive notifications */
        fd = VARSERVER_Signalfd( 0 );

        while ( 1 )
        {
            /* wait for a signal */
            sig = VARSERVER_WaitSignalfd( fd, &sigval );

            if ( ( sig == SIG_VAR_MODIFIED ) &&
                 ( pState->hTriggerVar == (VAR_HANDLE)sigval ) )
            {
                if ( pState->verbose == true )
                {
                    printf("Saving all dirty variables\n");
                }

                /* Create the variable configuration file */
                result = InitConfig( pState );
                if ( result == EOK )
                {
                    result = WriteConfig( pState );
                    if ( result == EOK )
                    {
                        result = FinalizeConfig( pState );
                    }
                }

                if ( result != EOK )
                {
                    fprintf( stderr,
                             "Failed to create configuration file: %s\n",
                             pState->filename );
                }
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  InitConfig                                                                */
/*!
    Initialize the configuration file

    The InitConfig function creates and opens a new temporary file
    for writing the dirty configuration data into.

    if the file is successfully created, pState->fd is a handle to
    the configuration file which was opened for writing

    @param[in,out]
        pState
            pointer to the SaveSvc state which contains the config file name

    @retval EOK - success
    @retval EINVAL - invalid arguments
    @retval other error from open()

==============================================================================*/
static int InitConfig( SaveSvcState *pState )
{
    int result = EINVAL;
    int n;
    int rc;

    if ( ( pState != NULL ) &&
         ( pState->filename != NULL ) )
    {
        /* create the temporary file name */
        n = snprintf( pState->tmpfile,
                      sizeof pState->tmpfile,
                      "%s.%s",
                      pState->filename,
                      ".tmp" );
        if ( n > 0 )
        {
            if ( (size_t)n < sizeof pState->tmpfile )
            {
                /* remove any previous file which may be left around */
                rc = unlink( pState->tmpfile );
                if ( rc == -1 )
                {
                    result = rc;
                }

                /* open the output file for creation/writing */
                pState->fd = open( pState->tmpfile, O_CREAT | O_WRONLY, 0644 );
                if ( pState->fd == -1 )
                {
                    /* an error occurred */
                    result = errno;
                }
                else
                {
                    /* file was opened ok */
                    result = EOK;
                }
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  WriteConfig                                                               */
/*!
    Write data to the configuration file

    The WriteConfig function iterates through all of the dirty configuration
    variables and writes them to the configuration file as var=value pairs.

    @param[in,out]
        pState
            pointer to the SaveSvc state which contains the config
            file descriptor

    @retval EOK - success
    @retval EINVAL - invalid arguments
    @retval other error from open()

==============================================================================*/
static int WriteConfig( SaveSvcState *pState )
{
    int result = EINVAL;
    char *config = "@config User Settings\n\n";
    ssize_t n;
    ssize_t len;

    if ( ( pState != NULL ) &&
         ( pState->fd != -1 ) )
    {
        /* write the file header */
        len = strlen(config);
        n = write( pState->fd, config, len );
        if ( n != len )
        {
            fprintf( stderr, "Header output failed\n" );
        }
        else
        {
            /* output all dirty variables */
            result = WriteConfigVars( pState );
        }

        /* close the output file */
        close( pState->fd );
        pState->fd = -1;

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  WriteConfigVars                                                           */
/*!
    Write dirty variables to the configuration file

    The WriteConfigVars function iterates through all of the dirty configuration
    variables and writes them to the configuration file as var=value pairs.

    @param[in,out]
        pState
            pointer to the SaveSvc state which contains the config
            file descriptor

    @retval EOK - success
    @retval EINVAL - invalid arguments

==============================================================================*/
static int WriteConfigVars( SaveSvcState *pState )
{
    int result = EINVAL;
    char buf[BUFSIZ];
    VarQuery query;
    VarObject obj;
    int rc;

    if ( pState != NULL )
    {
        memset( &query, 0, sizeof( VarQuery ) );

        query.type = QUERY_FLAGS;
        query.flags = VARFLAG_DIRTY;

        obj.val.str = buf;
        obj.len = sizeof buf;

        result = VAR_GetFirst( pState->hVarServer, &query, &obj );
        while ( result == EOK )
        {
            if ( obj.type == VARTYPE_STR )
            {
                /* we already have a string object on the buffer */
                rc = EOK;
            }
            else
            {
                /* convert non-string object to string */
                rc = VAROBJECT_ToString( &obj, buf, sizeof buf);
            }

            if ( rc == EOK )
            {
                if ( query.instanceID == 0 )
                {
                    dprintf( pState->fd,
                             "%s=%s\n",
                             query.name,
                             buf );
                }
                else
                {
                    dprintf( pState->fd,
                             "[%d]%s=%s\n",
                             query.instanceID,
                             query.name,
                             buf );
                }
            }
            else
            {
                printf("cannot save %s: rc=%s\n", query.name, strerror(rc) );
            }

            obj.val.str = buf;
            obj.len = sizeof buf;

            result = VAR_GetNext( pState->hVarServer, &query, &obj );
        }
    }

    return result;
}

/*============================================================================*/
/*  FinalizeConfig                                                            */
/*!
    Finalize the configuration file

    The FinalizeConfig function moves the configuration data written
    to the temporary file into the final configuration file via a
    rename operation.  This ensures that there is never a time when
    the configuration file does not exist (except for on first startup
    when no configuration data has been saved)

    @param[in]
        pState
            pointer to the SaveSvc state which contains the name of the
            configuration file.

    @retval EOK - success
    @retval EINVAL - invalid arguments
    @retval other error from rename()

==============================================================================*/
static int FinalizeConfig( SaveSvcState *pState )
{
    int result = EINVAL;
    int rc;

    if ( ( pState != NULL ) &&
         ( pState->tmpfile != NULL ) &&
         ( pState->filename != NULL ) )
    {
        rc = rename( pState->tmpfile, pState->filename );
        result = ( rc == 0 ) ? EOK : errno;
    }

    return result;
}

/*============================================================================*/
/*  SetupTerminationHandler                                                   */
/*!
    Set up an abnormal termination handler

    The SetupTerminationHandler function registers a termination handler
    function with the kernel in case of an abnormal termination of this
    process.

==============================================================================*/
static void SetupTerminationHandler( void )
{
    static struct sigaction sigact;

    memset( &sigact, 0, sizeof(sigact) );

    sigact.sa_sigaction = TerminationHandler;
    sigact.sa_flags = SA_SIGINFO;

    sigaction( SIGTERM, &sigact, NULL );
    sigaction( SIGINT, &sigact, NULL );

}

/*============================================================================*/
/*  TerminationHandler                                                        */
/*!
    Abnormal termination handler

    The TerminationHandler function will be invoked in case of an abnormal
    termination of this process.  The termination handler closes
    the connection with the variable server and cleans up its VARFP shared
    memory.

@param[in]
    signum
        The signal which caused the abnormal termination (unused)

@param[in]
    info
        pointer to a siginfo_t object (unused)

@param[in]
    ptr
        signal context information (ucontext_t) (unused)

==============================================================================*/
static void TerminationHandler( int signum, siginfo_t *info, void *ptr )
{
    syslog( LOG_ERR, "Abnormal termination of vars\n" );

    /* signal number, context pointer and signal info are unused */
    (void)signum;
    (void)info;
    (void)ptr;

    if ( pState != NULL )
    {
        if ( pState->hVarServer != NULL )
        {
            VARSERVER_Close( pState->hVarServer );
            pState->hVarServer = NULL;
        }

        free( pState );
        pState = NULL;
    }

    exit( 1 );
}


