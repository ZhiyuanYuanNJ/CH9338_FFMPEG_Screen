#include "avformat.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "url.h"

#define CONFIG_CH375_PROTOCOL 1
#if CONFIG_CH375_PROTOCOL
#define		MAX_DEVICE_PATH_SIZE	128
#define WRITE_BUFSIZE 16384 // send buffer size
#define READ_BUFSIZE (WRITE_BUFSIZE * 2)
typedef enum {
    CH375_DOWNLOAD,
    CH375_UPLOAD,
    CH375_INVALID
} CH375_Type;
CH375_Type type = CH375_INVALID;
#include <windows.h>
#ifdef _WIN32
typedef int(__stdcall  * pCH375Open)(unsigned long iIndex);

typedef int(__stdcall * pCH375Close)(unsigned long iIndex);

typedef void *(__stdcall * pCH375GetDeviceName)(unsigned long iIndex);

typedef unsigned long(__stdcall * pCH375ClearBufUpload)(
    unsigned long iIndex,        /* Specify equipment serial number */
    unsigned long iPipeNum);      /* Endpoint number, valid values are 1 to 8 */

typedef unsigned long(__stdcall * pCH375SetTimeout)(
	unsigned long iIndex,        /* Specify equipment serial number */
	unsigned long iWriteTimeout, /* Specifies the timeout period for USB
					write out data blocks, in milliseconds
					mS, and 0xFFFFFFFF specifies no timeout
					(default) */
	unsigned long iReadTimeout); /* Specifies the timeout period for USB
					reading data blocks, in milliseconds mS,
					and 0xFFFFFFFF specifies no timeout
					(default) */

typedef unsigned long(__stdcall * pCH375WriteData)(
    unsigned long iIndex,        /* Specify equipment serial number */
    void *iBuffer,                 /* Point to a buffer large enough to
					  hold the descriptor */
    unsigned long *ioLength);     /* Pointing to the length unit, the input
					 is the length to be read, and the
					 return is the actual read length */

typedef unsigned long(__stdcall * pCH375WriteEndP)(
	unsigned long iIndex,         /* Specify equipment serial number */
    unsigned long iPipeNum,      /* Endpoint number, valid values are 1 to 8 */
	void *oBuffer,                /* Point to a buffer large enough to hold
					 the descriptor */
	unsigned long *ioLength);     /* Pointing to the length unit, the input
					 is the length to be read, and the
					 return is the actual read length */

typedef unsigned long(__stdcall * pCH375ReadEndP)(
	unsigned long iIndex,          /* Specify equipment serial number */
    unsigned long iPipeNum,      /* Endpoint number, valid values are 1 to 8 */
	void *iBuffer,                 /* Point to a buffer large enough to
					  hold the descriptor */
	unsigned long *ioLength);      /* Pointing to the length unit, the input
					  is the length to be read, and the
					  return is the actual read length */

typedef unsigned long(__stdcall * pCH375SetBufUploadEx)(
    unsigned long iIndex,          /* Specify equipment serial number */
    unsigned long iEnableOrClear,  /* 0 disables internal buffer upload mode,
                                      non-zero enables internal buffer upload mode
                                      and clears existing data in the buffer */
    unsigned long iPipeNum,        /* Endpoint number, valid values are 1 to 8 */
    unsigned long BufSize          /* Size of each buffer, maximum 4MB */
);

typedef unsigned long(__stdcall * pCH375SetBufDownloadEx)(
    unsigned long iIndex,          /* Specify equipment serial number */
    unsigned long iEnableOrClear,  /* 0 disables internal buffer upload mode,
                                      non-zero enables internal buffer upload mode
                                      and clears existing data in the buffer */
    unsigned long iPipeNum,        /* Endpoint number, valid values are 1 to 8 */
    unsigned long iPacketNum          /* Size of each buffer, maximum 4MB */
);

typedef unsigned long(__stdcall * pCH375SetExclusive)(
    unsigned long iIndex,          /* Specify equipment serial number */
    unsigned long iExclusive /* 0 disables exclusive mode, non-zero enables exclusive mode */
);

typedef unsigned long(__stdcall * pCH375SetBufDownload)(
    unsigned long iIndex,          /* Specify equipment serial number */
    unsigned long iEnableOrClear /* 0 disables internal buffer download mode,
                                      non-zero enables internal buffer download mode
                                      and clears existing data in the buffer */
);

typedef unsigned long(__stdcall * pCH375SetBufUpload)(
    unsigned long iIndex,          /* Specify equipment serial number */
    unsigned long iEnableOrClear /* 0 disables internal buffer download mode,
                                      non-zero enables internal buffer download mode
                                      and clears existing data in the buffer */
);

typedef unsigned long(__stdcall * pCH375QueryBufUploadEx)(
    unsigned long iIndex,          /* Specify equipment serial number */
    unsigned long iPipeNum,        /* Endpoint number, valid values are 1 to 8 */
    unsigned long *oPacketNum,       /* Pointing to the length unit, the input
                                      is the length to be read, and the
                                      return is the actual read length */
    unsigned long *oTotalLen       /* Pointing to the length unit, the input
                                      is the length to be read, and the
                                      return is the actual read length */
);

HMODULE uhModule = 0;
pCH375Open CH375Open;
pCH375Close CH375Close;
pCH375GetDeviceName CH375GetDeviceName;
pCH375ClearBufUpload CH375ClearBufUpload;
pCH375SetTimeout CH375SetTimeout;
pCH375ReadEndP CH375ReadEndP;
pCH375WriteData CH375WriteData;
pCH375WriteEndP CH375WriteEndP;
pCH375SetBufUploadEx CH375SetBufUploadEx;
pCH375SetBufDownloadEx CH375SetBufDownloadEx;
pCH375SetBufDownload CH375SetBufDownload;
pCH375SetExclusive CH375SetExclusive;
pCH375SetBufUpload CH375SetBufUpload;
pCH375QueryBufUploadEx CH375QueryBufUploadEx;
#endif

typedef struct CH375Context {
    const AVClass *class;
    int dev_index;
    int w_endpoint;
    int r_endpoint;
    int rw_timeout;
    char* dev_name;
} CH375Context;
#define OFFSET(x) offsetof(CH375Context, x)
static const AVOption ch375_options[] = {
    { "device_index", "CH375 device serial number", OFFSET(dev_index), AV_OPT_TYPE_INT, { .i64 = 0 }, -1, 12, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { "w_endpoint", "CH375 write endpoint", OFFSET(w_endpoint), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 8, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { "r_endpoint", "CH375 read endpoint", OFFSET(r_endpoint), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 8, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { "rw_timeout", "CH375 read/write timeout in milliseconds", OFFSET(rw_timeout), AV_OPT_TYPE_INT, { .i64 = 30 }, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { "dev_name", "CH375 device identifier", OFFSET(dev_name), AV_OPT_TYPE_STRING, {.str = "VID_1A86&PID_8026&MI_01"}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { NULL }
};

static int serch_device(CH375Context* c)
{
    char* pdevName = NULL;
    char  devName[MAX_DEVICE_PATH_SIZE] = "";
    for (ULONG i = 0; i < 3; i++)
    {
        pdevName = (char*)CH375GetDeviceName(i);
        if (pdevName != NULL)
        {
            strcpy_s(devName, MAX_DEVICE_PATH_SIZE, pdevName);
            CharUpperBuffA(devName, strlen(devName));
            if (strstr(devName, c->dev_name) != NULL)
            {
                c->dev_index = i;
                return 1;
            }
        }
    }
    return 0;
}

static CH375_Type check_ch375_type(const char *filename) {
    // 是否以 "ch375://" 开头
    const char *prefix = "ch375://";
    const char *target = NULL;
    size_t prefix_len = strlen(prefix);
    
    if (strncmp(filename, prefix, prefix_len) != 0) {
        return CH375_INVALID; // 不匹配协议头
    }
    
    // （"download" 或 "upload"）
    target = filename + prefix_len;
    
    if (strcmp(target, "download") == 0) {
        return CH375_DOWNLOAD;
    } else if (strcmp(target, "upload") == 0) {
        return CH375_UPLOAD;
    }  
    return CH375_INVALID;
}
static int ch375_open(URLContext *h, const char *filename, int flags)
{
    int ret;
    CH375Context *c = NULL;
    type = check_ch375_type(filename);
    if (type == CH375_INVALID) {
        av_log(h, AV_LOG_ERROR, "Invalid CH375 protocol type\n");
        return AVERROR_EXTERNAL;
    }
    #ifdef _WIN32
	if (uhModule == 0) {
        #ifdef _WIN64
		uhModule = LoadLibrary("WCHKMFU64.dll");
		#else
		uhModule = LoadLibrary("WCHKMFU.dll");
		#endif
		if (uhModule) {
			CH375Open = (pCH375Open)GetProcAddress(
				uhModule, "CH375OpenDevice");
			CH375Close = (pCH375Close)GetProcAddress(
				uhModule, "CH375CloseDevice");
            CH375GetDeviceName = (pCH375GetDeviceName)GetProcAddress(
                uhModule, "CH375GetDeviceName");
            CH375ClearBufUpload = (pCH375ClearBufUpload)GetProcAddress(
                uhModule, "CH375ClearBufUpload");
			CH375ReadEndP = (pCH375ReadEndP)GetProcAddress(
				uhModule, "CH375ReadEndP");
            CH375WriteData = (pCH375WriteData)GetProcAddress(
                uhModule, "CH375WriteData");
			CH375WriteEndP = (pCH375WriteEndP)GetProcAddress(
				uhModule, "CH375WriteEndP");
			CH375SetTimeout = (pCH375SetTimeout)GetProcAddress(
				uhModule, "CH375SetTimeout");
            CH375SetBufUploadEx = (pCH375SetBufUploadEx)GetProcAddress(
                uhModule, "CH375SetBufUploadEx");
            CH375SetBufUpload = (pCH375SetBufUpload)GetProcAddress(
                uhModule, "CH375SetBufUpload");
            CH375SetBufDownload = (pCH375SetBufDownload)GetProcAddress(
                uhModule, "CH375SetBufDownload");
            CH375SetExclusive = (pCH375SetExclusive)GetProcAddress(
                uhModule, "CH375SetExclusive");
            CH375QueryBufUploadEx = (pCH375QueryBufUploadEx)GetProcAddress(
                uhModule, "CH375QueryBufUploadEx");
			if (CH375Open == NULL || CH375Close == NULL || CH375GetDeviceName == NULL
			    || CH375ClearBufUpload == NULL || CH375SetTimeout == NULL || CH375ReadEndP == NULL
			    || CH375WriteData == NULL|| CH375WriteEndP == NULL 
                || CH375SetBufUploadEx == NULL || CH375SetExclusive == NULL 
                || CH375SetBufDownload == NULL || CH375SetBufUpload == NULL 
                || CH375QueryBufUploadEx == NULL) {
				av_log(h, AV_LOG_ERROR, "Failed to init CH375 device\n");
                printf("Failed to init CH375 device\n");
				return -1;
			}
		}
	}
#endif
    c = h->priv_data;
    if (!c) {
        return AVERROR_EXTERNAL;
    }
    if (serch_device(c) == 0) {
        printf("Can not find ch9338 device\n");
        av_log(h, AV_LOG_ERROR, "Can not find ch9338 device\n");
        return AVERROR_EXTERNAL;
    }
    ret = CH375Open(c->dev_index);
    if (ret < 0) {
        printf("open failed\n");
        av_log(h, AV_LOG_ERROR, "Failed to open ch9338 device %d\n", c->dev_index);
        return AVERROR_EXTERNAL;
    }
    c->dev_index = ret;
    printf("ch9338 open success\n");
    CH375SetTimeout(c->dev_index, c->rw_timeout, c->rw_timeout);
    if (type == CH375_DOWNLOAD) {
        if (CH375SetBufDownload(c->dev_index, 0) == 0) {
            printf("CH375SetBufDownload failed\n");
        }
    } else if (type == CH375_UPLOAD) {
        if (CH375SetExclusive(c->dev_index, 1) == 0) {
            printf("CH375SetExclusive failed\n");
        }
        if (CH375SetBufUploadEx(c->dev_index, 1, c->r_endpoint, READ_BUFSIZE) == 0) {
            printf("CH375SetBufUploadEx failed\n");
        }
    }
    return 0;
}

static int ch375_read(URLContext *h, uint8_t *buf, int size)
{   
    static int b_first = 1;
    int ret;
    CH375Context *c = h->priv_data;
    unsigned long chunk_size = 0;
    unsigned long read_size = 0;
    if (b_first) {
        CH375ClearBufUpload(c->dev_index,c->r_endpoint);
        b_first = 0;
    }
    while (read_size < size) {
        chunk_size = (size - read_size) > READ_BUFSIZE ? READ_BUFSIZE : (size - read_size);
        ret = CH375ReadEndP(c->dev_index, c->r_endpoint, (void *)(buf + read_size), &chunk_size);
        if (ret == 1) {
            read_size += chunk_size;
            if (chunk_size == 0) {
                return read_size;
            }
        } else {
            av_log(h, AV_LOG_ERROR, "CH375 read error: %d\n", ret);
            // return AVERROR_EXTERNAL;
        }
        
    }
    return read_size;
}

static int ch375_write(URLContext *h, const uint8_t *buf, int size)
{
    int ret;
    CH375Context *c = h->priv_data;
    unsigned long written_size = 0;
    unsigned long chunk_size = 0;
    while (written_size < size) {
        chunk_size = (size - written_size) > WRITE_BUFSIZE ? WRITE_BUFSIZE : (size - written_size);
        ret = CH375WriteEndP(c->dev_index, c->w_endpoint, (void *)(buf + written_size), &chunk_size);
        if (ret == 1) {
            written_size += chunk_size;
        } else {
            av_log(h, AV_LOG_ERROR, "CH375 write error: %d\n", ret);
            return AVERROR_EXTERNAL;
        }
    }
    return written_size;
}

static int ch375_close(URLContext *h)
{
    CH375Context *c = h->priv_data;
    if (type == CH375_UPLOAD) {
        CH375SetExclusive(c->dev_index, 0);
        CH375SetBufUploadEx(c->dev_index, 0, c->r_endpoint, READ_BUFSIZE);
    }
    return 0;
}

static int ch375_get_handle(URLContext *h)
{
    CH375Context *c = h->priv_data;
    return c->dev_index;
}

static const AVClass ch375_class = {
    .class_name = "ch375",
    .item_name  = av_default_item_name,
    .option     = ch375_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_ch375_protocol = {
    .name                = "ch375",
    .url_open            = ch375_open,
    .url_read            = ch375_read,
    .url_write           = ch375_write,
    .url_close           = ch375_close,
    .url_get_file_handle = ch375_get_handle,
    .priv_data_size      = sizeof(CH375Context),
    .priv_data_class     = &ch375_class,
};

#endif /* CONFIG_CH375_PROTOCOL */