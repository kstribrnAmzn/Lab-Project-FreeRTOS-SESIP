/*
 * FreeRTOS Provision Interface v0.0.1
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */
/* Logging stack. */
#include "logging_levels.h"

#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME    "PROVISION_INTERFACE"
#endif

#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif

#include "logging_stack.h"
#include "stddef.h"

#include "FreeRTOS.h"
#include "task.h"
#include "provision_interface.h"
#include "provision.h"
#include "core_pkcs11.h"
#include "core_pkcs11_pal.h"

#define FILENAME_AWS_THING_NAME "aws_thing_name.dat"
#define FILENAME_AWS_ENDPOINT   "aws_endpoint.dat"

#define MAX_LENGTH_AWS_ENDPOINT   64
#define MAX_LENGTH_AWS_THING_NAME 32

/*
 * @brief Buffer size for buffer that will contain certificate
 *
 * @note Should need waaaaaaaaay less bytes, since we are only doing ECDSA currently, but an RSA cert will probably
 * be around 4096 bytes, so we will leave some buffer room.
 */
#define CERTIFICATE_SIZE    5000

const char pucTerminaterString[] = ">>>>>>";

static void prvUploadCsr( void )
{
    uint8_t * pucCsr = NULL;

    LogInfo( ( "Creating CSR" ) );
    pucCsr = vCreateCsr();

    if( pucCsr == NULL )
    {
        LogError( ( "Failed to retrieve a CSR. Cannot continue with provisioning operation." ) );
    }
    else
    {
        LogInfo( ( "Outputting CSR:" ) );
        LogInfo( ( "\n%s", pucCsr ) );
        LogInfo( ( "Finished outputting CSR." ) );
        vPortFree( pucCsr );
    }
}

static uint32_t xReadInput( uint8_t * pucBuffer,
                            uint32_t ulSize,
                            const char * pcTermString,
                            uint32_t ulTermStringLen )
{
    uint32_t i = 0;
    uint32_t ulTermIter = 0;
    uint32_t ulWritten = 0;
    char cInput = 0x00;

    for( i = 0; i < ( ulSize + ulTermStringLen ); i++ )
    {
        cInput = DbgConsole_Getchar();

        if( cInput == pcTermString[ ulTermIter ] )
        {
            ulTermIter++;
        }
        else
        {
            ulTermIter = 0;
            pucBuffer[ ulWritten ] = cInput;
            ulWritten++;
        }

        /* Ignore NULL in term string. */
        if( ( ulWritten > ulSize ) || ( ulTermIter == ulTermStringLen - 1 ) )
        {
            break;
        }
    }

    return ulWritten;
}

static uint8_t * prvReadCertifcate( uint32_t * pulSize )
{
    uint8_t * pucCert = pvPortMalloc( CERTIFICATE_SIZE );

    memset( pucCert, 0x00, CERTIFICATE_SIZE );

    if( pucCert != NULL )
    {
        LogInfo( ( "Ready to read device certificate." ) );
        *pulSize = xReadInput( pucCert, CERTIFICATE_SIZE, pucTerminaterString, sizeof( pucTerminaterString ) );
    }
    else
    {
        LogError( ( "Failed to create a buffer big enough to hold the device certificate." ) );
    }

    return pucCert;
}

static CK_RV xDestroyCertKeys( void )
{
    CK_RV xResult = CKR_OK;
    xResult = xDestroyCryptoObjects();
    return xResult;
}

static CK_RV prvProvisionThingEndpoint( void )
{
    CK_RV xResult = CKR_OK;
    CK_ULONG ulSize = 0;
    CK_BYTE_PTR pxThingEndpoint[ MAX_LENGTH_AWS_ENDPOINT ] = { 0 };
    CK_ATTRIBUTE xLabel;
    CK_OBJECT_HANDLE xHandle = CK_INVALID_HANDLE;
    
    
    LogInfo( ("Ready to read thing endpoint." ) );

    ulSize = xReadInput( pxThingEndpoint, MAX_LENGTH_AWS_ENDPOINT, pucTerminaterString, sizeof( pucTerminaterString ) );

    if( ulSize > 0 )
    {
        xLabel.type = CKA_LABEL;
        xLabel.pValue = FILENAME_AWS_ENDPOINT;
        xLabel.ulValueLen = sizeof( FILENAME_AWS_ENDPOINT ); 

        LogInfo( ( "Saving thing endpoint: %s", pxThingEndpoint ) );

        xHandle = PKCS11_PAL_SaveObject( &xLabel, pxThingEndpoint, ulSize );
        if( xHandle == CK_INVALID_HANDLE )
        {
            LogError( ( "Failed to save thing endpoint. Error storing to flash, error code: %0x.", xResult ) );
        }
    }
    else
    {
        LogError( ( "Failed to save thing endpoint. Received no bytes over the UART." ) );
    }

    return xResult;
}

static CK_RV prvProvisionThingName( void )
{
    CK_RV xResult = CKR_OK;
    CK_ULONG ulSize = 0;
    CK_BYTE_PTR pxThingName[ MAX_LENGTH_AWS_THING_NAME ] = { 0 };
    CK_ATTRIBUTE xLabel;
    CK_OBJECT_HANDLE xHandle = CK_INVALID_HANDLE;

    LogInfo( ("Ready to read thing name." ) );

    ulSize = xReadInput( pxThingName, MAX_LENGTH_AWS_THING_NAME, pucTerminaterString, sizeof( pucTerminaterString ) );

    if( ulSize > 0 )
    {
        xLabel.type = CKA_LABEL;
        xLabel.pValue = FILENAME_AWS_THING_NAME;
        xLabel.ulValueLen = sizeof( FILENAME_AWS_THING_NAME ); 

        LogInfo( ( "Saving thing name: %s", pxThingName ) );

        xHandle = PKCS11_PAL_SaveObject( &xLabel, pxThingName, ulSize );
        if( xHandle == CK_INVALID_HANDLE )
        {
            LogError( ( "Failed to save thing name. Error storing to flash, error code: %0x.", xResult ) );
        }
    }
    else
    {
        LogError( ( "Failed to save thing name. Received no bytes over the UART." ) );
    }

    return xResult;
}

static void prvProvision( void )
{
    uint8_t ucInput = 0x00;
    uint8_t * pucCert = NULL;
    uint32_t ulCertSize = 0;

    LogInfo( ( "Do you want to provision the device? y/n" ) );

    ( void ) xReadInput( &ucInput, sizeof( char ), pucTerminaterString, sizeof( pucTerminaterString ) );

    if( ucInput == ( uint8_t ) 'y' )
    {
        LogInfo( ( "Received y, will provision the device." ) );
        prvProvisionThingName();
        prvProvisionThingEndpoint();
        prvUploadCsr();
        pucCert = prvReadCertifcate( &ulCertSize );

        if( pucCert != NULL )
        {
            LogInfo( ( "Successfully read UART cert from UART. Will now try to provision certificate with PKCS #11." ) );
            LogInfo( ( "Received:\n %s", pucCert ) );
            xProvisionCert( pucCert, ulCertSize );
            vPortFree( pucCert );
        }
    }
    else
    {
        LogInfo( ( "Will not provision the device." ) );
    }
}
void vUartProvision( void )
{
    CK_RV xResult = CKR_OK;

    LogInfo( ( "Starting Provisioning process..." ) );
    xResult = xCheckIfProvisioned();
    uint8_t ucInput = 0x00;

    if( xResult == CKR_OK )
    {
        LogInfo( ( "Device was already provisioned, should the current credentials be removed? y/n" ) );
        ( void ) xReadInput( &ucInput, sizeof( char ), pucTerminaterString, sizeof( pucTerminaterString ) );

        if( ucInput == ( uint8_t ) 'y' )
        {
            xResult = xDestroyCertKeys();

            if( xResult == CKR_OK )
            {
                LogInfo( ( "Successfully removed old objects." ) );
                prvProvision();
            }
        }
    }
    else
    {
        prvProvision();
    }
}
