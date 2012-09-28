/********************************************************************************
 Copyright (C) 2012 Hugh Bailey <obs.jim@gmail.com>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
********************************************************************************/


#include "Main.h"
#include "RTMPStuff.h"



class FLVFileStream : public VideoFileStream
{
    XFileOutputSerializer fileOut;
    String strFile;

    UINT64 metaDataPos;
    DWORD lastTimeStamp;

    bool bSentFirstPacket;

    void AppendFLVPacket(LPBYTE lpData, UINT size, BYTE type, DWORD timestamp)
    {
        UINT networkDataSize  = fastHtonl(size);
        UINT networkTimestamp = fastHtonl(timestamp);
        UINT streamID = 0;
        fileOut.OutputByte(type);
        fileOut.Serialize(((LPBYTE)(&networkDataSize))+1,  3);
        fileOut.Serialize(((LPBYTE)(&networkTimestamp))+1, 3);
        fileOut.Serialize(&networkTimestamp, 1);
        fileOut.Serialize(&streamID, 3);
        fileOut.Serialize(lpData, size);
        fileOut.OutputDword(fastHtonl(size+14));

        lastTimeStamp = timestamp;
    }

public:
    bool Init(CTSTR lpFile)
    {
        strFile = lpFile;

        if(!fileOut.Open(lpFile, XFILE_CREATEALWAYS, 1024*1024))
            return false;

        fileOut.OutputByte('F');
        fileOut.OutputByte('L');
        fileOut.OutputByte('V');
        fileOut.OutputByte(1);
        fileOut.OutputByte(5); //bit 0 = (hasAudio), bit 2 = (hasAudio)
        fileOut.OutputDword(DWORD_BE(9));
        fileOut.OutputDword(0);

        metaDataPos = fileOut.GetPos();

        char  metaDataBuffer[2048];
        char *enc = metaDataBuffer;
        char *pend = metaDataBuffer+sizeof(metaDataBuffer);

        enc = AMF_EncodeString(enc, pend, &av_onMetaData);
        char *endMetaData  = App->EncMetaData(enc, pend);
        UINT  metaDataSize = endMetaData-metaDataBuffer;

        AppendFLVPacket((LPBYTE)metaDataBuffer, metaDataSize, 18, 0);
        return true;
    }

    ~FLVFileStream()
    {
        UINT64 fileSize = fileOut.GetPos();
        fileOut.Close();

        XFile file;
        if(file.Open(strFile, XFILE_WRITE, XFILE_OPENEXISTING))
        {
            double doubleFileSize = double(fileSize);
            double doubleDuration = double(lastTimeStamp/1000);

            file.SetPos(metaDataPos+0x24, XFILE_BEGIN);
            QWORD outputVal = *reinterpret_cast<QWORD*>(&doubleDuration);
            outputVal = fastHtonll(outputVal);
            file.Write(&outputVal, 8);

            file.SetPos(metaDataPos+0x37, XFILE_BEGIN);
            outputVal = *reinterpret_cast<QWORD*>(&doubleFileSize);
            outputVal = fastHtonll(outputVal);
            file.Write(&outputVal, 8);

            file.Close();
        }
    }

    virtual void AddPacket(BYTE *data, UINT size, DWORD timestamp, PacketType type)
    {
        if(!bSentFirstPacket)
        {
            bSentFirstPacket = true;

            DataPacket audioHeaders, videoHeaders, videoSEI;
            App->GetAudioHeaders(audioHeaders);
            App->GetAudioHeaders(videoHeaders);
            App->GetVideoEncoder()->GetSEI(videoSEI);

            AppendFLVPacket(audioHeaders.lpPacket, audioHeaders.size, 8, 0);
            AppendFLVPacket(videoHeaders.lpPacket, videoHeaders.size, 9, 0);
            AppendFLVPacket(videoSEI.lpPacket,     videoSEI.size, 9, 0);
        }

        AppendFLVPacket(data, size, (type == PacketType_Audio) ? 8 : 9, timestamp);
    }
};


VideoFileStream* CreateFLVFileStream(CTSTR lpFile)
{
    FLVFileStream *fileStream = new FLVFileStream;
    if(fileStream->Init(lpFile))
        return fileStream;

    delete fileStream;
    return NULL;
}