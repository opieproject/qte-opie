/****************************************************************************
** $Id: qt/src/kernel/qsoundqss_qws.cpp   2.3.10   edited 2005-01-24 $
**
** Implementation of Qt Sound System
**
** Created : 001017
**
** Copyright (C) 2000 Trolltech AS.  All rights reserved.
**
** This file is part of the kernel module of the Qt GUI Toolkit.
**
** This file may be distributed and/or modified under the terms of the
** GNU General Public License version 2 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.
**
** Licensees holding valid Qt Enterprise Edition or Qt Professional Edition
** licenses for Qt/Embedded may use this file in accordance with the
** Qt Embedded Commercial License Agreement provided with the Software.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.trolltech.com/pricing.html or email sales@trolltech.com for
**   information about Qt Commercial License Agreements.
** See http://www.trolltech.com/gpl/ for GPL licensing information.
**
** Contact info@trolltech.com if any conditions of this licensing are
** not clear to you.
**
**********************************************************************/

#include "qsoundqss_qws.h"

#ifndef QT_NO_SOUND

#include <qlist.h>
#include <qsocketnotifier.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qserversocket.h>
#include <qtimer.h>

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <alsa/asoundlib.h>

#define DEFAULT_SND_DEVICE "default"

#define QT_QWS_SOUND_16BIT 1 // or 0, or undefined for always 0
#define QT_QWS_SOUND_STEREO 1 // or 0, or undefined for always 0

#define SOUND_BUFFER_TIME 200000 /* uS */
#define SOUND_PERIOD_TIME 50000 /* uS */

static const int sound_buffer_size = (44100 * (QT_QWS_SOUND_16BIT + 1) * (QT_QWS_SOUND_STEREO + 1)) / 2;

// Zaurus SL5000D doesn't seem to return any error if setting to 44000 and it fails,
// however 44100 works, 44100 is more common that 44000.
static int sound_speed = 44100;
#ifndef QT_NO_SOUNDSERVER
extern int qws_display_id;
#define SOUND_PIPE	"/tmp/.qt_soundserver-%1"
#endif

struct QRiffChunk {
    char id[4];
    Q_UINT32 size;
    char data[4/*size*/];
};

class QWSSoundServerClient : public QWSSocket {
    Q_OBJECT

public:
    QWSSoundServerClient(int s, QObject* parent);
    ~QWSSoundServerClient();

public slots:
    void sendSoundCompleted(int, int);
    void sendDeviceReady(int, int);
    void sendDeviceError(int, int, int);

signals:
    void play(int, int, const QString&);
    void play(int, int, const QString&, int, int);
    void playRaw(int, int, const QString&, int, int, int, int);

    void pause(int, int);
    void stop(int, int);
    void resume(int, int);
    void setVolume(int, int, int, int);
    void setMute(int, int, bool);

    void stopAll(int);

    void playPriorityOnly(bool);

private slots:
    void destruct();
    void tryReadCommand();

private:
    int mCurrentID;
    int left, right;
    bool priExist;
    static int lastId;
    static int nextId() { return ++lastId; }
};

int QWSSoundServerClient::lastId = 0;


#ifndef QT_NO_SOUNDSERVER
QWSSoundServerClient::QWSSoundServerClient(int s, QObject* parent) :
    QWSSocket(parent)
{
    priExist = FALSE;
    mCurrentID = nextId();
    setSocket(s);
    connect(this,SIGNAL(readyRead()),
	this,SLOT(tryReadCommand()));
    connect(this,SIGNAL(connectionClosed()),
	this,SLOT(destruct()));
}

QWSSoundServerClient::~QWSSoundServerClient()
{
    if (priExist)
	playPriorityOnly(FALSE);
    emit stopAll(mCurrentID);
}

void QWSSoundServerClient::destruct()
{
    delete this;
}

QString getStringTok(QString &in)
{
    int pos = in.find(' ');
    QString ret;
    if (pos > 0) {
	ret = in.left(pos);
	in = in.mid(pos+1);
    } else {
	ret = in;
	in = QString::null;
    }
    return ret;
}

int getNumTok(QString &in)
{
    return getStringTok(in).toInt();
}

void QWSSoundServerClient::tryReadCommand()
{
    while ( canReadLine() ) {
	QString l = readLine();
	l.truncate(l.length()-1); // chomp
	QString functionName = getStringTok(l);
	int soundid = getNumTok(l);
	if ( functionName == "PLAY" ) {
	    emit play(mCurrentID, soundid, l);
	} else if ( functionName == "PLAYEXTEND" ) {
	    int volume = getNumTok(l);
	    int flags = getNumTok(l);
	    emit play(mCurrentID, soundid, l, volume, flags);
	} else if ( functionName == "PLAYRAW" ) {
	    int chs = getNumTok(l);
	    int freq = getNumTok(l);
	    int bitspersample = getNumTok(l);
	    int flags = getNumTok(l);
	    emit playRaw(mCurrentID, soundid, l, chs, freq, bitspersample, flags);
	} else if ( functionName == "PAUSE" ) { 
	    emit pause(mCurrentID, soundid);
	} else if ( functionName == "STOP" ) { 
	    emit stop(mCurrentID, soundid);
	} else if ( functionName == "RESUME" ) { 
	    emit resume(mCurrentID, soundid);
	} else if ( functionName == "SETVOLUME" ) {
	    int left = getNumTok(l);
	    int right = getNumTok(l);
	    emit setVolume(mCurrentID, soundid, left, right);
	} else if ( functionName == "MUTE" ) {
	    emit setMute(mCurrentID, soundid, true);
	} else if ( functionName == "UNMUTE" ) {
	    emit setMute(mCurrentID, soundid, false);
	} else if ( functionName == "PRIORITYONLY" ) {
	    bool sPri = soundid != 0;
	    if (sPri != priExist) {
		priExist = sPri;
		emit playPriorityOnly(sPri);
	    }
	}
    }
}

void QWSSoundServerClient::sendSoundCompleted(int gid, int sid)
{
    if (gid == mCurrentID) {
	QCString u = QString("SOUNDCOMPLETED " 
		+ QString::number(sid) + "\n").utf8();
	writeBlock(u.data(), u.length());
	flush();
    }
}

void QWSSoundServerClient::sendDeviceReady(int gid, int sid)
{
    if (gid == mCurrentID) {
        QCString u = QString("DEVICEREADY " 
                + QString::number(sid) + "\n").utf8();
        writeBlock(u.data(), u.length());
	flush();
    }
}

void QWSSoundServerClient::sendDeviceError(int gid, int sid, int err)
{
    if (gid == mCurrentID) {
        QCString u = QString("DEVICEERROR " 
                + QString::number(sid) + " "
                + QString::number(err) + "\n").utf8();
        writeBlock(u.data(), u.length());
	flush();
    }
}
#endif

static const int maxVolume = 100;
class QWSSoundServerProvider {
public:
    QWSSoundServerProvider(int w, int s)
	: mWid(w), mSid(s), mMuted(false)
    {
	leftVolume = maxVolume>>1;
	rightVolume = maxVolume>>1;
	isPriority = FALSE;
	samples_due = 0;
	max1 = max2 = out = 0;//= sound_buffer_size;
	data = data1;
	max = &max1;
	sampleRunin = 0;
	dev = -1;
    }

    virtual ~QWSSoundServerProvider() {
    }

    int groupId() const { return mWid; }
    int soundId() const { return mSid; }

    void startSampleRunin() {
	// inteded to provide even audio return from mute/pause/dead samples.
	//sampleRunin = runinLength; // or more?
    }

    void setVolume(int lv, int rv) {
	leftVolume = QMIN(maxVolume, QMAX(0, lv));
	rightVolume = QMIN(maxVolume, QMAX(0, rv));
    }

    void setMute(bool m) { mMuted = m; }
    bool muted() { return mMuted; }

    void setPriority(bool p) {
	if (p != isPriority) {
	    isPriority = p; // currently meaningless.
	}
    }

    static void setPlayPriorityOnly(bool p)
    {
	if (p)
	    priorityExists++;
	else
	    priorityExists--;

	if (priorityExists < 0)
	    qDebug("QSS: got more priority offs than ons");
    }

    // return -1 for file broken, give up.
    // else return sampels ready for playing.
    // argument is max samples server is looking for,
    // in terms of current device status.
    virtual int readySamples(int) = 0;

    int getSample(int off, int bps) {
        return (bps == 1) ? (data[out+off] - 128) * 128 : ((short*)data)[(out/2)+off];
    }

    int add(int* mixl, int* mixr, int count)
    {
        int bytesPerSample = chunkdata.wBitsPerSample >> 3;

        if ( mMuted ) {
            sampleRunin -= QMIN(sampleRunin,count);
            while (count && (dev != -1)) {
                if (out >= *max) {
                    // switch buffers
                    out = 0;
                    if (data == data1 && max2 != 0) {
                        data = data2;
                        max = &max2;
                        max1 = 0;
                    } else if (data == data2 && max1 != 0) {
                        data = data1;
                        max = &max1;
                        max2 = 0;
                    } else {
                        qDebug("QSS Read Error: both buffers empty");
                        return 0;
                    }
                }
                samples_due += sound_speed;
                while (count && samples_due >= chunkdata.samplesPerSec) {
                    samples_due -= chunkdata.samplesPerSec;
                    count--;
                }
                out += bytesPerSample * chunkdata.channels;
            }
            return count;
        }

        // This shouldn't be the case
        if ( !mixl || !mixr )
            return 0;

        int lVolNum = leftVolume, lVolDen = maxVolume;
        int rVolNum = rightVolume, rVolDen = maxVolume;
        if (priorityExists > 0 && !isPriority) {
            lVolNum = 0; // later, make this gradually fade in and out.
            lVolDen = 5;
            rVolNum = 0;
            rVolDen = 5;
        }

        while (count && (dev != -1)) {
            if (out >= *max) {
                // switch buffers
                out = 0;
                if (data == data1 && max2 != 0) {
                    data = data2;
                    max = &max2;
                    max1 = 0;
                } else if (data == data2 && max1 != 0) {
                    data = data1;
                    max = &max1;
                    max2 = 0;
                } else {
                    qDebug("QSS Read Error: both buffers empty");
                    return 0;
                }
            }
            samples_due += sound_speed;
            if (count && samples_due >= chunkdata.samplesPerSec) {
                int l = getSample(0,bytesPerSample)*lVolNum/lVolDen;
                int r = (chunkdata.channels == 2) ? getSample(1,bytesPerSample)*rVolNum/rVolDen : l;
                if (!QT_QWS_SOUND_STEREO && chunkdata.channels == 2) 
                    l += r;
		if (sampleRunin) {
                    while (sampleRunin && count && samples_due >= chunkdata.samplesPerSec) {
                        mixl++;
                        if (QT_QWS_SOUND_STEREO)
                            mixr++;
                        samples_due -= chunkdata.samplesPerSec;
		        sampleRunin--;
                        count--;
                    }
                }
                while (count && samples_due >= chunkdata.samplesPerSec) {
                    *mixl++ += l;
                    if (QT_QWS_SOUND_STEREO)
                        *mixr++ += r;
                    samples_due -= chunkdata.samplesPerSec;
                    count--;
                }
            }

            // optimize out manipulation of sample if downsampling and we skip it
            out += bytesPerSample * chunkdata.channels;
        }

        return count;
    }

    virtual bool finished() const = 0;

    bool equal(int wid, int sid)
    {
	return (wid == mWid && sid == mSid);
    }

protected:

    char * prepareBuffer( int &size)
    {
	// keep reading as long as there is 50 % or more room in off buffer.
	if (data == data1 && (max2<<1 < sound_buffer_size)) {
	    size=sound_buffer_size - max2;
	    return (char *)data2;
	} else if (data == data2 && (max1<<1 < sound_buffer_size)) {
	    size=sound_buffer_size - max1;
	    return (char *)data1;
	} else {
	    size = 0;
	    return 0;
	}
    }

    void updateBuffer(int read)
    {
	// always reads to off buffer.
	if (read >= 0) {
	    if (data == data2) {
		max1 = read;
	    } else {
		max2 = read;
	    }
	}
    }

    int devSamples()
    {
	int possible = (((max1+max2-out) / ((chunkdata.wBitsPerSample>>3)*chunkdata.channels))
		*sound_speed)/chunkdata.samplesPerSec;

	return possible;
    }


    struct {
	Q_INT16 formatTag;
	Q_INT16 channels;
	Q_INT32 samplesPerSec;
	Q_INT32 avgBytesPerSec;
	Q_INT16 blockAlign;
	Q_INT16 wBitsPerSample;
    } chunkdata;
    int dev;
    int samples_due;
private:
    int mWid;
    int mSid;
    int leftVolume;
    int rightVolume;
    bool isPriority;
    static int priorityExists;
    int *max;
    uchar *data;
    uchar data1[sound_buffer_size+4]; // +4 to handle badly aligned input data
    uchar data2[sound_buffer_size+4]; // +4 to handle badly aligned input data
    int out, max1, max2;
    int sampleRunin;
    bool mMuted;
};

int QWSSoundServerProvider::priorityExists = 0;

class QWSSoundServerBucket : public QWSSoundServerProvider {
public:
    QWSSoundServerBucket(int d, int wid, int sid)
	: QWSSoundServerProvider(wid, sid)
    {
	dev = d;
	wavedata_remaining = -1;
	mFinishedRead = FALSE;
	mInsufficientSamples = FALSE;
    }
    ~QWSSoundServerBucket()
    {
	//dev->close();
	::close(dev);
    }
    bool finished() const
    {
	//return !max;
	return mInsufficientSamples && mFinishedRead ;
    }
    int readySamples(int)
    {
	int size;
	char *dest = prepareBuffer(size);
	// may want to change this to something like
	// if (data == data1 && max2<<1 < sound_buffer_size 
	//	||
	//	data == data2 && max1<<1 < sound_buffer_size)
	// so will keep filling off buffer while there is +50% space left
	if (size > 0 && dest != 0) {
	    while ( wavedata_remaining < 0 ) {
		//max = 0;
		wavedata_remaining = -1;
		// Keep reading chunks...
		const int n = sizeof(chunk)-sizeof(chunk.data);
		int nr = ::read(dev, (void*)&chunk,n);
		if ( nr != n ) {
		    // XXX check error? or don't we care?
		    wavedata_remaining = 0;
		    mFinishedRead = TRUE;
		} else if ( qstrncmp(chunk.id,"data",4) == 0 ) {
		    wavedata_remaining = chunk.size;

		    //out = max = sound_buffer_size;

		} else if ( qstrncmp(chunk.id,"RIFF",4) == 0 ) {
		    char d[4];
		    if ( read(dev, d, 4) != 4 ) {
			// XXX check error? or don't we care?
			//qDebug("couldn't read riff");
			mInsufficientSamples = TRUE;
			mFinishedRead = TRUE;
			return 0;
		    } else if ( qstrncmp(d,"WAVE",4) != 0 ) {
			// skip
			if ( chunk.size > 1000000000 || lseek(dev,chunk.size-4, SEEK_CUR) == -1 ) {
			    //qDebug("oversized wav chunk");
			    mFinishedRead = TRUE;
			}
		    }
		} else if ( qstrncmp(chunk.id,"fmt ",4) == 0 ) {
		    if ( ::read(dev,(char*)&chunkdata,sizeof(chunkdata)) != sizeof(chunkdata) ) {
			// XXX check error? or don't we care?
			//qDebug("couldn't ready chunkdata");
			mFinishedRead = TRUE;
		    }
#define WAVE_FORMAT_PCM 1
		    else if ( chunkdata.formatTag != WAVE_FORMAT_PCM ) {
			//qDebug("WAV file: UNSUPPORTED FORMAT %d",chunkdata.formatTag);
			mFinishedRead = TRUE;
		    }
		} else {
		    // ignored chunk
		    if ( chunk.size > 1000000000 || lseek(dev, chunk.size, SEEK_CUR) == -1) {
			//qDebug("chunk size too big");
			mFinishedRead = TRUE;
		    }
		}
	    }
	    // this looks wrong.
	    if (wavedata_remaining <= 0) {
		mFinishedRead = TRUE;
	    }
	}
	// may want to change this to something like
	// if (data == data1 && max2<<1 < sound_buffer_size 
	//	||
	//	data == data2 && max1<<1 < sound_buffer_size)
	// so will keep filling off buffer while there is +50% space left

	if (wavedata_remaining) {
	    if (size > 0 && dest != 0) {
		int read = ::read(dev, dest, QMIN(size, wavedata_remaining));
		// XXX check error? or don't we care?
		wavedata_remaining -= read;
		updateBuffer(read);
		if (read <= 0) // data unexpectidly ended
		    mFinishedRead = TRUE;
	    }
	}
	int possible = devSamples();
	if (possible == 0)
	    mInsufficientSamples = TRUE;
	return possible;
    }

protected:
    QRiffChunk chunk;
    int wavedata_remaining;
    bool mFinishedRead;
    bool mInsufficientSamples;
};

class QWSSoundServerStream : public QWSSoundServerProvider {
public:
    QWSSoundServerStream(int d,int c, int f, int b,
	    int wid, int sid)
	: QWSSoundServerProvider(wid, sid)
    {
	chunkdata.channels = c;
	chunkdata.samplesPerSec = f;
	chunkdata.wBitsPerSample = b;
	dev = d;
	//fcntl( dev, F_SETFL, O_NONBLOCK );
	lasttime = 0;
    }

    ~QWSSoundServerStream()
    {
	if (dev != -1) {
	    ::close(dev);
	    dev = -1;
	}
    }

    bool finished() const
    {
	return (dev == -1);
    }


    int readySamples(int)
    {
	int size;
	char *dest = prepareBuffer(size);
	if (size > 0 && dest != 0 && dev != -1) {

	    int read = ::read(dev, dest, size);
	    if (read < 0) {
		switch(errno) {
		    case EAGAIN:
		    case EINTR:
			// means read may yet succeed on the next attempt
			break;
		    default:
			// unexpected error, fail.
			::close(dev);
			dev = -1;
		}
	    } else if (read == 0) {
		// 0 means writer has closed dev and/or
		// file is at end.
		::close(dev);
		dev = -1;
	    } else {
		updateBuffer(read);
	    }
	}
	int possible = devSamples();
	if (possible == 0)
	    startSampleRunin();
	return possible;
    }

protected:
    time_t lasttime;
};

#ifndef QT_NO_SOUNDSERVER
QWSSoundServerSocket::QWSSoundServerSocket(QObject* parent, const char* name) :
    QWSServerSocket(QString(SOUND_PIPE).arg(qws_display_id), 0, parent, name)
{
}


void QWSSoundServerSocket::newConnection(int s)
{
    QWSSoundServerClient* client = new QWSSoundServerClient(s,this);

    connect(client, SIGNAL(play(int, int, const QString&)),
	this, SIGNAL(playFile(int, int, const QString&)));
    connect(client, SIGNAL(play(int, int, const QString&, int, int)),
	this, SIGNAL(playFile(int, int, const QString&, int, int)));
    connect(client, SIGNAL(playRaw(int, int, const QString&, int, int, int, int)),
	this, SIGNAL(playRawFile(int, int, const QString&, int, int, int, int)));

    connect(client, SIGNAL(pause(int, int)),
	this, SIGNAL(pauseFile(int, int)));
    connect(client, SIGNAL(stop(int, int)),
	this, SIGNAL(stopFile(int, int)));
    connect(client, SIGNAL(playPriorityOnly(bool)),
	this, SIGNAL(playPriorityOnly(bool)));
    connect(client, SIGNAL(stopAll(int)),
	this, SIGNAL(stopAll(int)));
    connect(client, SIGNAL(resume(int, int)),
	this, SIGNAL(resumeFile(int, int)));

    connect(client, SIGNAL(setMute(int, int, bool)),
	this, SIGNAL(setMute(int, int, bool)));
    connect(client, SIGNAL(setVolume(int, int, int, int)),
	this, SIGNAL(setVolume(int, int, int, int)));

    connect(this, SIGNAL(soundFileCompleted(int, int)),
	    client, SLOT(sendSoundCompleted(int, int)));
    connect(this, SIGNAL(deviceReady(int, int)),
	    client, SLOT(sendDeviceReady(int, int)));
    connect(this, SIGNAL(deviceError(int, int, int)),
	    client, SLOT(sendDeviceError(int, int, int)));
}
#endif

class QWSSoundServerData : public QObject {
    Q_OBJECT

public:
    QWSSoundServerData(QObject* parent=0, const char* name=0) :
	QObject(parent, name)
    {
	timerId = 0;
#ifndef QT_NO_SOUNDSERVER
	server = new QWSSoundServerSocket(this);

	connect(server, SIGNAL(playFile(int, int, const QString&)),
		this, SLOT(playFile(int, int, const QString&)));
	connect(server, SIGNAL(playFile(int, int, const QString&, int, int)),
		this, SLOT(playFile(int, int, const QString&, int, int)));
	connect(server, SIGNAL(playRawFile(int, int, const QString&, int, int, int, int)),
		this, SLOT(playRawFile(int, int, const QString&, int, int, int, int)));

	connect(server, SIGNAL(pauseFile(int, int)),
		this, SLOT(pauseFile(int, int)));
	connect(server, SIGNAL(stopFile(int,int)),
		this, SLOT(stopFile(int,int)));
	connect(server, SIGNAL(stopAll(int)),
		this, SLOT(stopAll(int)));
	connect(server, SIGNAL(playPriorityOnly(bool)),
		this, SLOT(playPriorityOnly(bool)));
	connect(server, SIGNAL(resumeFile(int, int)),
		this, SLOT(resumeFile(int, int)));

        connect(server, SIGNAL(setMute(int, int, bool)),
                this, SLOT(setMute(int, int, bool)));
	connect(server, SIGNAL(setVolume(int, int, int, int)),
		this, SLOT(setVolume(int, int, int, int)));

	connect(this, SIGNAL(soundFileCompleted(int, int)),
		server, SIGNAL(soundFileCompleted(int, int)));
	connect(this, SIGNAL(deviceReady(int, int)),
		server, SIGNAL(deviceReady(int, int)));
	connect(this, SIGNAL(deviceError(int, int, int)),
		server, SIGNAL(deviceError(int, int, int)));
#endif
    handle = NULL;
	active.setAutoDelete(TRUE);
	unwritten = 0;
    }

signals:
    void soundFileCompleted(int, int);
    void deviceReady(int, int);
    void deviceError(int, int, int);

public slots:
    void playRawFile(int wid, int sid, const QString &filename, int freq, int channels, int bitspersample, int flags)
    {
	int f = openFile(wid, sid, filename);
	if ( f ) {
	    QWSSoundServerStream *b = new QWSSoundServerStream(f, channels, freq, bitspersample, wid, sid);
	    // check preset volumes.
	    checkPresetVolumes(wid, sid, b);
	    b->setPriority((flags & QWSSoundClient::Priority) == QWSSoundClient::Priority);
	    active.append(b);
	    emit deviceReady(wid, sid);
	}
    }

    void playFile(int wid, int sid, const QString& filename)
    {
	int f = openFile(wid, sid, filename);
	if ( f ) {
	    QWSSoundServerProvider *b = new QWSSoundServerBucket(f, wid, sid);
	    checkPresetVolumes(wid, sid, b);
	    active.append( b );
	    emit deviceReady(wid, sid);
	}
    }

    void playFile(int wid, int sid, const QString& filename, int v, int flags)
    {
	int f = openFile(wid, sid, filename);
	if ( f ) {
	    QWSSoundServerProvider *b = new QWSSoundServerBucket(f, wid, sid);
	    checkPresetVolumes(wid, sid, b);
	    b->setVolume(v, v);
	    b->setPriority((flags & QWSSoundClient::Priority) == QWSSoundClient::Priority);
	    active.append(b);
	    emit deviceReady(wid, sid);
	}
    }

    void checkPresetVolumes(int wid, int sid, QWSSoundServerProvider *p) {
	QValueList<PresetVolume>::Iterator it = volumes.begin();
	while (it != volumes.end()) {
	    PresetVolume v = *it;
	    if (v.wid == wid && v.sid == sid) {
		p->setVolume(v.left, v.right);
		p->setMute(v.mute);
		it = volumes.remove(it);
		return;
	    } else {
		++it;
	    }
	}
    }

    void pauseFile(int wid, int sid)
    {
	QWSSoundServerProvider *bucket;
	for (bucket = active.first(); bucket; bucket = active.next()) {
	    if (bucket->equal(wid, sid)) {
		// found bucket....
		active.take();
		inactive.append(bucket);
		return;
	    }
	}
    }
    void resumeFile(int wid, int sid)
    {
	QWSSoundServerProvider *bucket;
	for (bucket = inactive.first(); bucket; bucket = inactive.next()) {
	    if (bucket->equal(wid, sid)) {
		// found bucket....
		inactive.take();
		active.append(bucket);
		return;
	    }
	}
    }

    void stopFile(int wid, int sid)
    {
	QWSSoundServerProvider *bucket;
	for (bucket = active.first(); bucket; bucket = active.next()) {
	    if (bucket->equal(wid, sid)) {
		active.removeRef(bucket);
		return;
	    }
	}
	for (bucket = inactive.first(); bucket; bucket = inactive.next()) {
	    if (bucket->equal(wid, sid)) {
		inactive.removeRef(bucket);
		return;
	    }
	}
    }

    void stopAll(int wid) {
	QWSSoundServerProvider *bucket;
	bucket = active.first(); 
	while(bucket) {
	    if (bucket->groupId() == wid) {
		active.removeRef(bucket);
		bucket = active.current();
	    } else {
		bucket = active.next();
	    }
	}
	bucket = inactive.first(); 
	while(bucket) {
	    if (bucket->groupId() == wid) {
		inactive.removeRef(bucket);
		bucket = inactive.current();
	    } else {
		bucket = inactive.next();
	    }
	}
    }

    void setVolume(int wid, int sid, int lv, int rv)
    {
	QWSSoundServerProvider *bucket;
	for (bucket = active.first(); bucket; bucket = active.next()) {
	    if (bucket->equal(wid, sid)) {
		bucket->setVolume(lv,rv);
		return;
	    }
	}
	// If gotten here, then it means wid/sid wasn't set up yet.
	// first find and remove current preset volumes, then add this one.
	QValueList<PresetVolume>::Iterator it = volumes.begin();
	while (it != volumes.end()) {
	    PresetVolume v = *it;
	    if (v.wid == wid && v.sid == sid)
		it = volumes.remove(it);
	    else
		++it;
	}
	// and then add this volume
	PresetVolume nv;
	nv.wid = wid;
	nv.sid = sid;
	nv.left = lv;
	nv.right = rv;
	nv.mute = FALSE;
	volumes.append(nv);
    }

    void setMute(int wid, int sid, bool m)
    {
	QWSSoundServerProvider *bucket;
	for (bucket = active.first(); bucket; bucket = active.next()) {
	    if (bucket->equal(wid, sid)) {
		bucket->setMute(m);
		return;
	    }
	}
	// if gotten here then setting is being applied before item
	// is created.
	QValueList<PresetVolume>::Iterator it = volumes.begin();
	while (it != volumes.end()) {
	    PresetVolume v = *it;
	    if (v.wid == wid && v.sid == sid) {
		(*it).mute = m;
		return;
	    }
	}
	if (m) {
	    PresetVolume nv;
	    nv.wid = wid;
	    nv.sid = sid;
	    nv.left = maxVolume>>1;
	    nv.right = maxVolume>>1;
	    nv.mute = TRUE;
	    volumes.append(nv);
	}
    }

    void playPriorityOnly(bool p) {
	QWSSoundServerProvider::setPlayPriorityOnly(p);
    }

    void sendCompletedSignals()
    {
	while( !completed.isEmpty() ) {
	    emit soundFileCompleted( (*completed.begin()).groupId,
		    (*completed.begin()).soundId );
	    completed.remove( completed.begin() );
	}
    }

    void feedDevice(void)
    {
	if ( !unwritten && active.count() == 0 ) {
	    closeDevice();
	    sendCompletedSignals();
	    return;
	} else {
	    sendCompletedSignals();
	}

	QWSSoundServerProvider* bucket;

	// find out how much audio is possible
	int available = period_size;
	QList<QWSSoundServerProvider> running;
	for (bucket = active.first(); bucket; bucket = active.next()) {
	    int ready = bucket->readySamples(available);
	    if (ready > 0) {
		available = QMIN(available, ready);
		running.append(bucket);
	    }
	}

    if ( !unwritten ) {
        int left[period_size];
        memset(left,0,available*sizeof(int));
        int right[period_size];
        if ( QT_QWS_SOUND_STEREO )
            memset(right,0,available*sizeof(int));

        if (running.count() > 0) {
            // should do volume mod here in regards to each bucket to avoid flattened/bad peaks.
            for (bucket = running.first(); bucket; bucket = running.next()) {
            int unused = bucket->add(left,right,available);
            if (unused > 0) {
                // this error is quite serious, as
                // it will really screw up mixing.
                qDebug("provider lied about samples ready");
            }
            }
            if (QT_QWS_SOUND_16BIT) {
            short *d = (short*)data;
            for (int i = 0; i < available; i++) {
                *d++ = (short)QMAX(QMIN(left[i],32767),-32768);
                if (QT_QWS_SOUND_STEREO) 
                    *d++ = (short)QMAX(QMIN(right[i],32767),-32768);
            }
            } else {
            signed char *d = (signed char *)data;
            for (int i = 0; i < available; i++) {
                *d++ = (signed char)QMAX(QMIN(left[i]/256,127),-128)+128;
                if (QT_QWS_SOUND_STEREO) 
                    *d++ = (signed char)QMAX(QMIN(right[i]/256,127),-128)+128;
            }
            }
            unwritten = available;
            cursor = (char*)data;
        }
    }
    // sound open, but nothing written.  Should clear the buffer.

    int err;
    if (unwritten) {
        int w = 0;
        do {
            err = snd_pcm_writei(handle, cursor, unwritten);
        } while (err == -EAGAIN);
        if (err < 0) {
            if (err == -EPIPE) {
                err = snd_pcm_prepare(handle);
                if (err < 0) {
                    qDebug("Can't recover after underrun\n");
                }
            } else if (err == -ESTRPIPE) {
                do {
                    err = snd_pcm_resume(handle);
                } while (err == -EAGAIN);
                err = snd_pcm_prepare(handle);
                if (err < 0) {
                    qDebug("Can't recover after underrun\n");
                }
            }
        }

        w = err;

        cursor += w;
        unwritten -= w;
    }

	bucket = active.first(); 
	while(bucket) {
	    if (bucket->finished()) {
		completed.append(CompletedInfo(bucket->groupId(), bucket->soundId()));
		active.removeRef(bucket);
		bucket = active.current();
	    } else {
		bucket = active.next();
	    }
	}
    }

protected:
    void timerEvent(QTimerEvent* event)
    {
	//qDebug("QSS: timerEvent");
	if ( event->timerId() == timerId ) {	
	    if (handle)
            feedDevice();
	    if (!handle) {
            killTimer(event->timerId());
            timerId = 0;
	    }
	}
    }

private:
    int openFile(int wid, int sid, const QString& filename)
    {
	stopFile(wid, sid); // close and re-open.
	int f = ::open(QFile::encodeName(filename), O_RDONLY|O_NONBLOCK); 
	if (f == -1) {
	    // XXX check ferror, check reason.
	    qDebug("Failed opening \"%s\"",filename.latin1());
	    emit deviceError(wid, sid, (int)QWSSoundClient::ErrOpeningFile );
        } else if ( openDevice() ) {
            return f;
	}
	emit deviceError(wid, sid, (int)QWSSoundClient::ErrOpeningAudioDevice );
	return 0;
    }

    bool openDevice()
    {
    snd_pcm_format_t format;
    snd_pcm_uframes_t size;
    unsigned int rate;
    int channels, dir, err;
    unsigned int buffer_time = SOUND_BUFFER_TIME;
    unsigned int period_time = SOUND_PERIOD_TIME;
    snd_pcm_hw_params_t *hwparams;
    snd_pcm_sw_params_t *swparams;
    if (!handle) {
        err = snd_pcm_open(&handle, DEFAULT_SND_DEVICE, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (err < 0) {
            qDebug("Failed opening audio device");
            return false;
        }

        snd_pcm_hw_params_alloca(&hwparams);
        snd_pcm_sw_params_alloca(&swparams);

        /* Setup soundcard */
        err = snd_pcm_hw_params_any(handle, hwparams);
        if (err < 0) {
            qDebug("No configuration available for playback: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        /* Enable alsa-lib resampling */
        err = snd_pcm_hw_params_set_rate_resample(handle, hwparams, 1);
        if (err < 0) {
            qDebug("Failed to enable alsalib resample: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        /* Set access type */
        err = snd_pcm_hw_params_set_access(handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0) {
            qDebug("Failed to set access type: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

#ifdef QT_QWS_SOUND_16BIT
        format = SND_PCM_FORMAT_S16;
#else
        format = SND_PCM_FORMAT_S8;
#endif
        err = snd_pcm_hw_params_set_format(handle, hwparams, format);
        if (err < 0) {
            qDebug("Failed to set format: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

#ifdef QT_QWS_SOUND_STEREO
        channels = 2;
#else
        channels = 1;
#endif
        err = snd_pcm_hw_params_set_channels(handle, hwparams, channels);
        if (err < 0) {
            qDebug("Failed to set channels count: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        rate = sound_speed;
        err = snd_pcm_hw_params_set_rate_near(handle, hwparams, &rate, 0);
        if (err < 0) {
            qDebug("Failed to set rate %dHz: %s\n", sound_speed, snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        err = snd_pcm_hw_params_set_buffer_time_near(handle, hwparams, &buffer_time, &dir);
        if (err < 0) {
            qDebug("Failed to set buffer time %duS: %s\n", buffer_time, snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        err = snd_pcm_hw_params_get_buffer_size(hwparams, &size);
        if (err < 0) {
            qDebug("Failed to get buffer size: %s", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }
        buffer_size = size;

        err = snd_pcm_hw_params_set_period_time_near(handle, hwparams, &period_time, &dir);
        if (err < 0) {
            qDebug("Failed to set period time %duS: %s\n", period_time, snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        err = snd_pcm_hw_params_get_period_size(hwparams, &size, NULL);
        if (err < 0) {
            qDebug("Failed to get period size: %s", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }
        period_size = size;

        /* Write the parameters to device */
        err = snd_pcm_hw_params(handle, hwparams);
        if (err < 0) {
            qDebug("Failed to apply hw params: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        /* Set SW params */
        err = snd_pcm_sw_params_current(handle, swparams);
        if (err < 0) {
            qDebug("Failed to determine current swparams for playback: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        err = snd_pcm_sw_params_set_start_threshold(handle, swparams, (buffer_size / period_size) * period_size);
        if (err < 0) {
            qDebug("Failed to set start threshold: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_size);
        if (err < 0) {
            qDebug("Failed to set avail min: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

        err = snd_pcm_sw_params(handle, swparams);
        if (err < 0) {
            qDebug("Failed to apply sw params: %s\n", snd_strerror(err));
            snd_pcm_close(handle);
            handle = NULL;
            return false;
        }

	    timerId = startTimer(period_time / 2000);
	}
	return TRUE;
    }

    void closeDevice()
    {
	if ( handle ) {
        snd_pcm_drain(handle);
        snd_pcm_close(handle);
        handle = NULL;
	}
    }

    int timerId;
    QList<QWSSoundServerProvider> active;
    QList<QWSSoundServerProvider> inactive;
    struct PresetVolume {
	int wid;
	int sid;
	int left;
	int right;
	bool mute;
    };
    QValueList<PresetVolume> volumes;
    struct CompletedInfo {
	CompletedInfo( ) : groupId( 0 ), soundId( 0 ) { }
	CompletedInfo( int _groupId, int _soundId ) : groupId( _groupId ), soundId( _soundId ) { }
	int groupId;
	int soundId;
    };
    QValueList<CompletedInfo> completed;
    snd_pcm_t *handle;
    snd_pcm_uframes_t buffer_size;
    snd_pcm_uframes_t period_size;
    int unwritten;
    char* cursor;
    short data[sound_buffer_size*2];
#ifndef QT_NO_SOUNDSERVER
    QWSSoundServerSocket *server;
#endif
};

QWSSoundServer::QWSSoundServer(QObject* parent) :
    QObject(parent)
{
    d = new QWSSoundServerData(this);
}

void QWSSoundServer::playFile( const QString& filename, int sid )
{
    //wid == 0, as it is the server initiating rather than a client
    // if wid was passable, would accidently collide with server
    // sockect's wids.
    d->playFile(0, sid, filename);
}

void QWSSoundServer::pauseFile( int sid )
{
    d->pauseFile(0, sid);
}

void QWSSoundServer::stopFile( int sid )
{
    d->stopFile(0, sid);
}

void QWSSoundServer::resumeFile( int sid )
{
    d->resumeFile(0, sid);
}

QWSSoundServer::~QWSSoundServer()
{
    d->stopAll(0);
}

#ifndef QT_NO_SOUNDSERVER
QWSSoundClient::QWSSoundClient( QObject* parent ) :
    QWSSocket(parent)
{
    connectToLocalFile(QString(SOUND_PIPE).arg(qws_display_id));
    connect(this,SIGNAL(readyRead()),
	this,SLOT(tryReadCommand()));
    if( state() == Connection ) QTimer::singleShot(1, this, SIGNAL(connected()));
    else QTimer::singleShot(1, this, SLOT(emitConnectionRefused()));
}

QWSSoundClient::~QWSSoundClient( )
{
    flush();
}

void QWSSoundClient::reconnect()
{
    connectToLocalFile(QString(SOUND_PIPE).arg(qws_display_id));
    if( state() == Connection ) emit connected();
    else emit error( ErrConnectionRefused );
}

// ### what about file names with spaces?????

void QWSSoundClient::play( int id, const QString& filename )
{
    QFileInfo fi(filename);
    QCString u = ("PLAY "
	    + QString::number(id)
            + " " + fi.absFilePath()
            + "\n").utf8();
    writeBlock(u.data(), u.length());
}

void QWSSoundClient::play( int id, const QString& filename, int volume, int flags)
{
    QFileInfo fi(filename);
    QCString u = ("PLAYEXTEND "
	    + QString::number(id)
	    + " " + QString::number(volume)
	    + " " + QString::number(flags)
	    + " " + fi.absFilePath() 
	    + "\n").utf8();
    writeBlock(u.data(), u.length());
}

void QWSSoundClient::pause( int id )
{
    QCString u = QString("PAUSE "
	    + QString::number(id)
            + "\n").utf8();
    writeBlock(u.data(), u.length());
}

void QWSSoundClient::stop( int id )
{
    QCString u = QString("STOP "
	    + QString::number(id)
            + "\n").utf8();
    writeBlock(u.data(), u.length());
}

void QWSSoundClient::resume( int id )
{
    QCString u = QString("RESUME "
	    + QString::number(id)
            + "\n").utf8();
    writeBlock(u.data(), u.length());
}

void QWSSoundClient::playRaw( int id, const QString& filename,
	int freq, int chs, int bitspersample, int flags)
{
    QFileInfo fi(filename);
    QCString u = ("PLAYRAW "
	    + QString::number(id)
	    + " " + QString::number(chs)
	    + " " + QString::number(freq)
	    + " " + QString::number(bitspersample)
	    + " " + QString::number(flags)
            + " " + fi.absFilePath()
	    + "\n").utf8();
    writeBlock(u.data(), u.length());
    flush();
}

void QWSSoundClient::setMute( int id, bool m )
{
    QCString u = ((m ? "MUTE " : "UNMUTE ")
            + QString::number(id)
	    + "\n").utf8();
    writeBlock(u.data(), u.length());
}

void QWSSoundClient::setVolume( int id, int leftVol, int rightVol )
{
    QCString u = ("SETVOLUME "
            + QString::number(id)
	    + " " + QString::number(leftVol)
            +  " " + QString::number(rightVol) +
	    + "\n").utf8();
    writeBlock(u.data(), u.length());
}

void QWSSoundClient::playPriorityOnly( bool pri )
{
    QCString u = ("PRIORITYONLY "
            + QString::number(pri ? 1 : 0)
	    + "\n").utf8();
    writeBlock(u.data(), u.length());
}


void QWSSoundClient::tryReadCommand()
{
    while ( canReadLine() ) {
	QString l = readLine();
	l.truncate(l.length()-1); // chomp
	QStringList token = QStringList::split(" ",l);
	if ( token[0] == "SOUNDCOMPLETED" ) {
	    emit soundCompleted(token[1].toInt());
	} else if ( token[0] == "DEVICEREADY" ) {
            emit deviceReady(token[1].toInt());
	} else if ( token[0] == "DEVICEERROR" ) {
            emit deviceError(token[1].toInt(),(DeviceErrors)token[2].toInt());
	}
    }
}

void QWSSoundClient::emitConnectionRefused()
{
    emit error( ErrConnectionRefused );
}
#endif

#include "qsoundqss_qws.moc"

#endif	// QT_NO_SOUND
