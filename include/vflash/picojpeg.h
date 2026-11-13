//
// picojpeg - Public domain, Rich Geldreich <richgel99@gmail.com>
//
#ifndef VFLASH_PICOJPEG_H
#define VFLASH_PICOJPEG_H

#ifdef __cplusplus
extern "C" {
#endif

//
// Status and error codes returned by PicoJPEG decompression functions.
//
enum
{
    PJPG_NO_MORE_BLOCKS = 1,
    PJPG_BAD_DHT_COUNTS,
    PJPG_BAD_DHT_INDEX,
    PJPG_BAD_DHT_MARKER,
    PJPG_BAD_DQT_MARKER,
    PJPG_BAD_DQT_TABLE,
    PJPG_BAD_PRECISION,
    PJPG_BAD_HEIGHT,
    PJPG_BAD_WIDTH,
    PJPG_TOO_MANY_COMPONENTS,
    PJPG_BAD_SOF_LENGTH,
    PJPG_BAD_VARIABLE_MARKER,
    PJPG_BAD_DRI_LENGTH,
    PJPG_BAD_SOS_LENGTH,
    PJPG_BAD_SOS_COMP_ID,
    PJPG_W_EXTRA_BYTES_BEFORE_MARKER,
    PJPG_NO_ARITHMITIC_SUPPORT,
    PJPG_UNEXPECTED_MARKER,
    PJPG_NOT_JPEG,
    PJPG_UNSUPPORTED_MARKER,
    PJPG_BAD_DQT_LENGTH,
    PJPG_TOO_MANY_BLOCKS,
    PJPG_UNDEFINED_QUANT_TABLE,
    PJPG_UNDEFINED_HUFF_TABLE,
    PJPG_NOT_SINGLE_SCAN,
    PJPG_UNSUPPORTED_COLORSPACE,
    PJPG_UNSUPPORTED_SAMP_FACTORS,
    PJPG_DECODE_ERROR,
    PJPG_BAD_RESTART_MARKER,
    PJPG_ASSERTION_ERROR,
    PJPG_BAD_SOS_SPECTRAL,
    PJPG_BAD_SOS_SUCCESSIVE,
    PJPG_STREAM_READ_ERROR,
    PJPG_NOTENOUGHMEM,
    PJPG_UNSUPPORTED_COMP_IDENT,
    PJPG_UNSUPPORTED_QUANT_TABLE,
    PJPG_UNSUPPORTED_MODE // Progressive JPEG streams are not supported
};

//
// Supported JPEG chroma subsampling scan types.
//
typedef enum
{
    PJPG_GRAYSCALE,
    PJPG_YH1V1,
    PJPG_YH2V1,
    PJPG_YH1V2,
    PJPG_YH2V2
} pjpeg_scan_type_t;

//
// Decoded JPEG image metadata and internal MCU component output buffers.
// Each block in m_pMCUBufR/G/B is a contiguous 64-byte (8x8) component
// block. PJPG_GRAYSCALE uses m_pMCUBufR only (1 block); PJPG_YH1V1 uses 1
// block per channel; PJPG_YH2V1/YH1V2 use 2 blocks (offsets 0, 64 / 0, 128);
// PJPG_YH2V2 uses 4 blocks (offsets 0, 64, 128, 192).
//
typedef struct
{
    int m_width;                 // Image width in pixels
    int m_height;                // Image height in pixels
    int m_comps;                 // Component count (1 or 3)
    int m_MCUSPerRow;            // MCU columns per row
    int m_MCUSPerCol;            // MCU rows per column
    pjpeg_scan_type_t m_scanType;// Chroma subsampling scan type
    int m_MCUWidth;              // MCU width in pixels (8 or 16)
    int m_MCUHeight;             // MCU height in pixels (8 or 16)
    unsigned char *m_pMCUBufR;   // Red or grayscale Y MCU buffer
    unsigned char *m_pMCUBufG;   // Green MCU buffer
    unsigned char *m_pMCUBufB;   // Blue MCU buffer
} pjpeg_image_info_t;

//
// Stream input callback invoked by PicoJPEG to refill its bitstream buffer.
//
// pBuf:                Destination byte buffer to fill.
// buf_size:            Maximum bytes to read into pBuf.
// pBytes_actually_read: Output count of bytes actually placed into pBuf.
// pCallback_data:      Opaque caller context pointer.
//
// Returns 0 on success, or non-zero on stream read failure.
//
typedef unsigned char (*pjpeg_need_bytes_callback_t)(
    unsigned char *pBuf, unsigned char buf_size,
    unsigned char *pBytes_actually_read, void *pCallback_data);

//
// Initialize the PicoJPEG decompressor and parse image headers.
// Not thread-safe.
//
// pInfo:                Output image metadata and MCU buffer pointers.
// pNeed_bytes_callback: Stream refill callback function.
// pCallback_data:       Opaque pointer passed to pNeed_bytes_callback.
// reduce:               Non-zero to decode only DC (1 pixel per block).
//
// Returns 0 on success, or a PJPG_* error code on failure.
//
unsigned char pjpeg_decode_init(pjpeg_image_info_t *pInfo,
                                pjpeg_need_bytes_callback_t pNeed_bytes_callback,
                                void *pCallback_data, unsigned char reduce);

//
// Decompress the next Minimum Coded Unit (MCU) into pInfo->m_pMCUBuf*.
// Must be called m_MCUSPerRow * m_MCUSPerCol times. Not thread-safe.
//
// Returns 0 on success, PJPG_NO_MORE_BLOCKS at end, or a PJPG_* error.
//
unsigned char pjpeg_decode_mcu(void);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_PICOJPEG_H
