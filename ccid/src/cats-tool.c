/*
 * Copyright (C) 2010 Frank Morgner
 *
 * This file is part of ifdnfc.
 *
 * ifdnfc is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * ifdnfc is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ifdnfc.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winscard.h>
#include <time.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ARPA_INIT_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_PCSCLITE_H
#include <pcsclite.h>
#endif

#ifdef HAVE_READER_H
#include <reader.h>
#endif


#ifndef FEATURE_EXECUTE_PACE
#define FEATURE_EXECUTE_PACE 0x20
#endif

#ifndef CM_IOCTL_GET_FEATURE_REQUEST
#define CM_IOCTL_GET_FEATURE_REQUEST SCARD_CTL_CODE(3400)
#endif

#define PCSC_TLV_ELEMENT_SIZE 6


static void
printb(const char *label, unsigned char *buf, size_t len)
{
    size_t i = 0;
    printf("%s", label);
    while (i < len) {
        printf("%02X", buf[i]);
        i++;
        if (i%20)
            printf(" ");
        else if (i != len)
            printf("\n");
    }
    printf("\n");
}

static LONG parse_EstablishPACEChannel_OutputData(
        unsigned char output[], unsigned int output_length)
{
    uint8_t lengthCAR, lengthCARprev;
    uint16_t lengthOutputData, lengthEF_CardAccess, length_IDicc;
    uint32_t result;
    size_t parsed = 0;

    if (parsed+4 > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    memcpy(&result, output+parsed, 4);
    parsed += 4;
    switch (result) {
        case 0x00000000:
            break;
        default:
            fprintf(stderr, "Reader reported some error.\n");
            return SCARD_F_COMM_ERROR;
    }

    if (parsed+2 > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    memcpy(&lengthOutputData, output+parsed, 2);
    parsed += 2;
    if (lengthOutputData != output_length-parsed) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }

    if (parsed+2 > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    printf("MSE:Set AT Statusbytes: %02X %02X\n",
            output[parsed+0], output[parsed+1]);
    parsed += 2;

    if (parsed+2 > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    memcpy(&lengthEF_CardAccess, output+parsed, 2);
    parsed += 2;

    if (parsed+lengthEF_CardAccess > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    if (lengthEF_CardAccess)
        printb("EF.CardAccess:\n", &output[parsed], lengthEF_CardAccess);
    parsed += lengthEF_CardAccess;

    if (parsed+1 > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    lengthCAR = output[parsed];
    parsed += 1;

    if (parsed+lengthCAR > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    if (lengthCAR)
        printb("Recent Certificate Authority:\n",
                &output[parsed], lengthCAR);
    parsed += lengthCAR;

    if (parsed+1 > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    lengthCARprev = output[parsed];
    parsed += 1;

    if (parsed+lengthCARprev > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    if (lengthCARprev)
        printb("Previous Certificate Authority:\n",
                &output[parsed], lengthCARprev);
    parsed += lengthCARprev;

    if (parsed+2 > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    memcpy(&length_IDicc , output+parsed, 2);
    parsed += 2;

    if (parsed+length_IDicc > output_length) {
        fprintf(stderr, "Malformed Establish PACE Channel output data.\n");
        return SCARD_F_INTERNAL_ERROR;
    }
    if (length_IDicc)
        printb("IDicc:\n", &output[parsed], length_IDicc);
    parsed += length_IDicc;

    if (parsed != output_length) {
        fprintf(stderr, "Overrun by %d bytes\n", output_length - parsed);
        return SCARD_F_INTERNAL_ERROR;
    }

    return SCARD_S_SUCCESS;
}

int
main(int argc, char *argv[])
{
    LONG r;
    SCARDCONTEXT hContext;
    SCARDHANDLE hCard;
    LPSTR readers = NULL;
    char *reader;
    BYTE sendbuf[16], recvbuf[1024];
    DWORD ctl, recvlen, readerslen;
    time_t t_start, t_end;
    size_t l, pinlen = 0;
    char *pin = NULL;
    unsigned int readernum = 0, i;


    if (argc > 1) {
        if (sscanf(argv[1], "%u", &readernum) != 1) {
            fprintf(stderr, "Could not get number of reader\n");
            exit(2);
        }
        if (argc > 2) {
            pin = argv[2];
            pinlen = strlen(pin);
            if (pinlen < 5 || pinlen > 6) {
                fprintf(stderr, "PIN too long\n");
                exit(2);
            }
        }
        if (argc > 3) {
            fprintf(stderr, "Usage:  "
                    "%s [reader number] [PIN]\n", argv[0]);
        }
    }


    r = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &hContext);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not connect to PC/SC Service\n");
        goto err;
    }


    readerslen = SCARD_AUTOALLOCATE;
    r = SCardListReaders(hContext, NULL, (LPSTR) &readers, &readerslen);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not get readers\n");
        goto err;
    }

    for (reader = readers, i = 0; readerslen > 0;
            l = strlen(reader) + 1, readerslen -= l, reader += l, i++) {

        if (i == readernum)
            break;
    }
    if (readerslen <= 0) {
        fprintf(stderr, "Could not find reader number %u\n", readernum);
        r = SCARD_E_UNKNOWN_READER;
        goto err;
    }


    r = SCardConnect(hContext, reader, SCARD_SHARE_DIRECT, 0, &hCard, &ctl); 
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not connect to %s\n", reader);
        goto err;
    }
    printf("Connected to %s\n", reader);


#define SIMULATE_BUERGERCLIENT 1
#ifdef SIMULATE_BUERGERCLIENT
    r = SCardReconnect(hCard, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0,
            SCARD_LEAVE_CARD, &ctl);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not reconnect to %s\n", reader);
        goto err;
    }
    BYTE bufs[] = {
            0xFF, 0x9A, 0x01, 0x01,
            0xFF, 0x9A, 0x01, 0x03,
            0xFF, 0x9A, 0x01, 0x06,
            0xFF, 0x9A, 0x01, 0x07,
            0x00, 0xA4, 0x02, 0x0C, 0x02, 0x4B, 0x31,
            0x00, 0xA4, 0x04, 0x0C, 0x09, 0xE8, 0x07, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x03, 0x02,
            0x00, 0xA4, 0x00, 0x0C, 0x02, 0x3F, 0x00,
            0x00, 0x22, 0xC1, 0xA4, 0x0F, 0x80, 0x0A, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x02, 0x02, 0x04, 0x02, 0x02, 0x83, 0x01, 0x03,
            0x00, 0xA4, 0x00, 0x0C, 0x02, 0x3F, 0x00,
            0x00, 0xA4, 0x02, 0x0C, 0x02, 0x01, 0x1C,
            0x00, 0xB0, 0x00, 0x00, 0x80,
            0x00, 0xB0, 0x00, 0x80, 0x80,
            0x00, 0xB0, 0x01, 0x00, 0x80,
            0x00, 0xB0, 0x01, 0x80, 0x80,
            0x00, 0xB0, 0x02, 0x00, 0x80,
            0x00, 0xA4, 0x00, 0x0C, 0x02, 0x3F, 0x00,
            0x00, 0x22, 0xC1, 0xA4, 0x0F, 0x80, 0x0A, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x02, 0x02, 0x04, 0x02, 0x02, 0x83, 0x01, 0x03,
    };
    LPBYTE buf = bufs;
    DWORD lens[] = {4, 4, 4, 4, 7, 14, 7, 20, 7, 7, 5, 5, 5, 5, 5, 7, 20};
    SCARD_IO_REQUEST pioRecvPci;
    for (l = 0; l < sizeof(lens)/sizeof(DWORD); l++) {
        recvlen = sizeof(recvbuf);
        r = SCardTransmit(hCard, SCARD_PCI_T0, buf, lens[l], &pioRecvPci,
                recvbuf, &recvlen);
        if (r != SCARD_S_SUCCESS) {
            fprintf(stderr, "Simulation of Buergerclient failed\n");
            goto err;
        }
        buf += lens[l];
    }
    r = SCardReconnect(hCard, SCARD_SHARE_DIRECT, 0, SCARD_LEAVE_CARD, &ctl);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not reconnect to %s\n", reader);
        goto err;
    }
#endif


    /* does the reader support PACE? */
    recvlen = sizeof(recvbuf);
    r = SCardControl(hCard, CM_IOCTL_GET_FEATURE_REQUEST, NULL, 0,
            recvbuf, sizeof(recvbuf), &recvlen);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not get the reader's features\n");
        goto err;
    }

    ctl = 0;
    for (i = 0; i <= recvlen-PCSC_TLV_ELEMENT_SIZE; i += PCSC_TLV_ELEMENT_SIZE) {
        if (recvbuf[i] == FEATURE_EXECUTE_PACE) {
            memcpy(&ctl, recvbuf+i+2, 4);
            break;
        }
    }
    if (0 == ctl) {
        printf("Reader does not support PACE\n");
        goto err;
    }
    /* convert to host byte order to use for SCardControl */
    ctl = ntohl(ctl);

    recvlen = sizeof(recvbuf);
    sendbuf[0] = 0x01;              /* idxFunction = GetReadersPACECapabilities */
    sendbuf[1] = 0x00;              /* lengthInputData */
    sendbuf[2] = 0x00;              /* lengthInputData */
    r = SCardControl(hCard, ctl,
            sendbuf, 3,
            recvbuf, sizeof(recvbuf), &recvlen);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not get reader's PACE capabilities\n");
        goto err;
    }
    printb("ReadersPACECapabilities ", recvbuf, recvlen);


    recvlen = sizeof(recvbuf);
    sendbuf[0] = 0x02;              /* idxFunction = EstabishPACEChannel */
    sendbuf[1] = (5+pinlen)&0xff;   /* lengthInputData */
    sendbuf[2] = (5+pinlen)>>8;     /* lengthInputData */
    sendbuf[3] = 0x03;              /* PACE with PIN */
    sendbuf[4] = 0x00,              /* length CHAT */
    sendbuf[5] = 0x00,              /* length PIN */
    sendbuf[6] = pinlen;            /* length PIN */
    memcpy(sendbuf+7, pin, pinlen); /* PIN */
    sendbuf[7+pinlen] = 0x00;       /* length certificate description */
    sendbuf[8+pinlen] = 0x00;       /* length certificate description */
    t_start = time(NULL);
    r = SCardControl(hCard, ctl,
            sendbuf, 8+pinlen,
            recvbuf, sizeof(recvbuf), &recvlen);
    t_end = time(NULL);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not establish PACE channel\n");
        goto err;
    }
    printf("EstablishPACEChannel successfull, received %d bytes\n",
            (int)recvlen);

    r = parse_EstablishPACEChannel_OutputData(recvbuf, recvlen);
    if (r != SCARD_S_SUCCESS)
        goto err;

    printf("Established PACE channel returned in %.0fs.\n",
            difftime(t_end, t_start));


#define SIMULATE_RESTRICTED_ID 1
#ifdef SIMULATE_RESTRICTED_ID
    r = SCardReconnect(hCard, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0,
            SCARD_LEAVE_CARD, &ctl);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not reconnect to %s\n", reader);
        goto err;
    }
    BYTE ridbufs[] = {
        0x00, 0x22, 0x81, 0xB6, 0x0F, 0x83, 0x0D, 0x5A, 0x5A, 0x43, 0x56, 0x43, 0x41, 0x41, 0x54, 0x41, 0x30, 0x30, 0x30, 0x31,
        0x00, 0x2A, 0x00, 0xBE, 0xE4, 0x7F, 0x4E, 0x81, 0x9D, 0x5F, 0x29, 0x01, 0x00, 0x42, 0x0D, 0x5A, 0x5A, 0x43, 0x56, 0x43, 0x41, 0x41, 0x54, 0x41, 0x30, 0x30, 0x30, 0x31, 0x7F, 0x49, 0x4F, 0x06, 0x0A, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x02, 0x02, 0x02, 0x02, 0x03, 0x86, 0x41, 0x04, 0x52, 0xDD, 0x32, 0xEA, 0xFE, 0x1F, 0xBB, 0xB4, 0x00, 0x0C, 0xD9, 0xCE, 0x75, 0xF6, 0x66, 0x36, 0xCF, 0xCF, 0x1E, 0xDD, 0x44, 0xF7, 0xB1, 0xED, 0xAE, 0x25, 0xB8, 0x41, 0x93, 0xDA, 0x04, 0xA9, 0x1C, 0x77, 0xEE, 0x87, 0xF5, 0xC8, 0xF9, 0x59, 0xED, 0x27, 0x62, 0x00, 0xDE, 0x33, 0xAB, 0x57, 0x4C, 0xE9, 0x80, 0x11, 0x35, 0xFF, 0x44, 0x97, 0xA3, 0x71, 0x62, 0xB7, 0xC8, 0x54, 0x8A, 0x0C, 0x5F, 0x20, 0x0E, 0x5A, 0x5A, 0x44, 0x56, 0x43, 0x41, 0x41, 0x54, 0x41, 0x30, 0x30, 0x30, 0x30, 0x35, 0x7F, 0x4C, 0x12, 0x06, 0x09, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x03, 0x01, 0x02, 0x02, 0x53, 0x05, 0x70, 0x03, 0x01, 0xFF, 0xB7, 0x5F, 0x25, 0x06, 0x01, 0x00, 0x00, 0x06, 0x01, 0x01, 0x5F, 0x24, 0x06, 0x01, 0x00, 0x01, 0x00, 0x03, 0x01, 0x5F, 0x37, 0x40, 0x6F, 0x13, 0xAE, 0x9A, 0x6F, 0x4E, 0xDD, 0xB7, 0x83, 0x9F, 0xF3, 0xF0, 0x4D, 0x71, 0xE0, 0xDC, 0x37, 0x7B, 0xC4, 0xB0, 0x8F, 0xAD, 0x29, 0x5E, 0xED, 0x24, 0x1B, 0x52, 0x43, 0x28, 0xAD, 0x07, 0x30, 0xEB, 0x55, 0x34, 0x97, 0xB4, 0xFB, 0x66, 0xE9, 0xBB, 0x7A, 0xB9, 0x08, 0x15, 0xF0, 0x42, 0x73, 0xF0, 0x9E, 0x75, 0x1D, 0x7F, 0xD4, 0xB8, 0x61, 0x43, 0x9B, 0x4E, 0xE6, 0x53, 0x81, 0xC3,
        0x00, 0x22, 0x81, 0xB6, 0x10, 0x83, 0x0E, 0x5A, 0x5A, 0x44, 0x56, 0x43, 0x41, 0x41, 0x54, 0x41, 0x30, 0x30, 0x30, 0x30, 0x35,
        0x00, 0x2A, 0x00, 0xBE, 0x00, 0x01, 0x41, 0x7F, 0x4E, 0x81, 0xFA, 0x5F, 0x29, 0x01, 0x00, 0x42, 0x0E, 0x5A, 0x5A, 0x44, 0x56, 0x43, 0x41, 0x41, 0x54, 0x41, 0x30, 0x30, 0x30, 0x30, 0x35, 0x7F, 0x49, 0x4F, 0x06, 0x0A, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x02, 0x02, 0x02, 0x02, 0x03, 0x86, 0x41, 0x04, 0x9B, 0xFE, 0x74, 0x15, 0xD7, 0x3C, 0x4A, 0x78, 0xD6, 0x0B, 0x2C, 0xC1, 0xBC, 0xA1, 0x1B, 0x6D, 0x5E, 0x52, 0x39, 0x69, 0xAC, 0xFB, 0x5B, 0x75, 0x6A, 0x3B, 0xE1, 0x55, 0x1B, 0x22, 0x23, 0x9C, 0x79, 0xAE, 0x36, 0x2B, 0x83, 0x8B, 0x00, 0x66, 0x99, 0x83, 0xC0, 0xCA, 0xF6, 0xED, 0x0C, 0x78, 0x1D, 0x40, 0x1C, 0x95, 0xD2, 0xB3, 0x28, 0x57, 0xDE, 0x8C, 0xE1, 0xB6, 0x19, 0xDA, 0xC4, 0xA7, 0x5F, 0x20, 0x0A, 0x5A, 0x5A, 0x53, 0x49, 0x54, 0x30, 0x30, 0x30, 0x4F, 0x34, 0x7F, 0x4C, 0x12, 0x06, 0x09, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x03, 0x01, 0x02, 0x02, 0x53, 0x05, 0x00, 0x00, 0x00, 0x00, 0x04, 0x5F, 0x25, 0x06, 0x01, 0x00, 0x00, 0x09, 0x02, 0x01, 0x5F, 0x24, 0x06, 0x01, 0x00, 0x00, 0x09, 0x02, 0x06, 0x65, 0x5E, 0x73, 0x2D, 0x06, 0x09, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x03, 0x01, 0x03, 0x02, 0x80, 0x20, 0xB0, 0x2B, 0xAA, 0x51, 0xA9, 0x4F, 0xAC, 0x09, 0x54, 0xDF, 0x20, 0x4D, 0x61, 0xFE, 0x22, 0xDA, 0x1D, 0x40, 0x8D, 0x45, 0xDB, 0x4A, 0xA1, 0xD7, 0x0E, 0x60, 0x0D, 0xAD, 0x4F, 0xAF, 0x67, 0x99, 0x73, 0x2D, 0x06, 0x09, 0x04, 0x00, 0x7F, 0x00, 0x07, 0x03, 0x01, 0x03, 0x01, 0x80, 0x20, 0xC7, 0x2E, 0x13, 0x58, 0x2F, 0x01, 0xBA, 0x06, 0x8D, 0xD1, 0xAA, 0xC2, 0x9A, 0x24, 0x28, 0xC0, 0xC5, 0x4A, 0xB9, 0xC2, 0x04, 0xFD, 0x53, 0xB3, 0xF1, 0x3E, 0x82, 0x90, 0xE2, 0x1E, 0x50, 0xF9, 0x5F, 0x37, 0x40, 0x83, 0xC5, 0xB4, 0x41, 0xFE, 0xC5, 0xB1, 0x8E, 0xFD, 0x1C, 0xAA, 0x4A, 0x11, 0xB8, 0xE1, 0xCE, 0xDE, 0x0A, 0x8B, 0x42, 0xD4, 0x42, 0xF0, 0x0D, 0x7F, 0x60, 0x4E, 0x42, 0x9F, 0x33, 0x9B, 0x4E, 0x3E, 0x6C, 0x06, 0xF9, 0xE7, 0x6A, 0x2D, 0xAA, 0x82, 0xC1, 0x72, 0x2E, 0xE1, 0x37, 0xA8, 0x90, 0x38, 0xB9, 0x69, 0xC6, 0x34, 0x56, 0x15, 0x81, 0xE6, 0xC2, 0x6D, 0x9F, 0x6F, 0xA7, 0x5C, 0x52,
    };
    LPBYTE ridbuf = ridbufs;
    DWORD ridlens[] = {20, 233, 21, 328};
    SCARD_IO_REQUEST ridpioRecvPci;
    for (l = 0; l < sizeof(ridlens)/sizeof(DWORD); l++) {
        recvlen = sizeof(recvbuf);
        r = SCardTransmit(hCard, SCARD_PCI_T0, ridbuf, ridlens[l], &ridpioRecvPci,
                recvbuf, &recvlen);
        if (r != SCARD_S_SUCCESS) {
            fprintf(stderr, "Simulation of Restricted Identification failed\n");
            goto err;
        }
        ridbuf += ridlens[l];
    }
    r = SCardReconnect(hCard, SCARD_SHARE_DIRECT, 0, SCARD_LEAVE_CARD, &ctl);
    if (r != SCARD_S_SUCCESS) {
        fprintf(stderr, "Could not reconnect to %s\n", reader);
        goto err;
    }
#endif


    r = SCardDisconnect(hCard, SCARD_LEAVE_CARD);
    if (r != SCARD_S_SUCCESS)
        goto err;

    r = SCardFreeMemory(hContext, readers);
    if (r != SCARD_S_SUCCESS)
        goto err;


    exit(0);

err:
#ifdef HAVE_PCSCLITE_H
    if (r != SCARD_S_SUCCESS)
        puts(pcsc_stringify_error(r));
#endif
    if (readers)
        SCardFreeMemory(hContext, readers);


    exit(1);
}