/*
 *  MPEGTS Segment Element
 *
 *  Mostly copy from /gst-plugins-bad/gst/mpegtsdemux
 */

#ifndef __TSSEGMENT_H__
#define __TSSEGMENT_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/mpegts/mpegts.h>
#include <gst/codecparsers/gsth264parser.h>

#define MPEGTS_NORMAL_PACKETSIZE 188
#define MPEGTS_M2TS_PACKETSIZE 192
#define MPEGTS_DVB_ASI_PACKETSIZE 204
#define MPEGTS_ATSC_PACKETSIZE 208
#define CONTINUITY_UNSET 255
#define VERSION_NUMBER_UNSET 255
#define TABLE_ID_UNSET 0xFF
#define PACKET_SYNC_BYTE 0x47
#define MPEGTS_MIN_PACKETSIZE MPEGTS_NORMAL_PACKETSIZE
#define MPEGTS_MAX_PACKETSIZE MPEGTS_ATSC_PACKETSIZE

#define MPEGTS_BIT_SET(field, offs)    ((field)[(offs) >> 3] |=  (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_IS_SET(field, offs) ((field)[(offs) >> 3] &   (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_UNSET(field, offs)  ((field)[(offs) >> 3] &= ~(1 << ((offs) & 0x7)))
#define FLAGS_CONTINUITY_COUNTER(f)    (f & 0x0f)
#define FLAGS_HAS_AFC(f)               (f & 0x20)
#define FLAGS_HAS_PAYLOAD(f)           (f & 0x10)
#define MPEGTS_AFC_PCR_FLAG            0x10

#define MAX_PCR 2576980378112

/* PCR_TO_GST calculation requires at least 10 extra bits.
 * Since maximum PCR value is coded with 42 bits, we are
 * safe to use direct calculation (10+42 < 63)*/
#define PCRTIME_TO_GSTTIME(t) (((t) * (guint64)1000) / 27)

#define SAFE_FOURCC_FORMAT "02x%02x%02x%02x (%c%c%c%c)"
#define SAFE_CHAR(a) (g_ascii_isalnum((gchar) (a)) ? ((gchar)(a)) : '.')
#define SAFE_FOURCC_ARGS(a)                             \
  ((guint8) ((a)>>24)),                                 \
    ((guint8) ((a) >> 16 & 0xff)),                      \
    ((guint8) a >> 8 & 0xff),                           \
    ((guint8) a & 0xff),                                \
    SAFE_CHAR((a)>>24),                                 \
    SAFE_CHAR((a) >> 16 & 0xff),                        \
    SAFE_CHAR((a) >> 8 & 0xff),                         \
    SAFE_CHAR(a & 0xff)

#define CONTINUITY_UNSET 255
#define MAX_CONTINUITY 15

typedef enum
{
        PENDING_PACKET_EMPTY = 0,     /* No pending packet/buffer
                                       * Push incoming buffers to the array */
        PENDING_PACKET_HEADER,        /* PES header needs to be parsed
                                       * Push incoming buffers to the array */
        PENDING_PACKET_BUFFER,        /* Currently filling up output buffer
                                       * Push incoming buffers to the bufferlist */
        PENDING_PACKET_DISCONT        /* Discontinuity in incoming packets
                                       * Drop all incoming buffers */
} PendingPacketState;

typedef struct
{
        guint16 pid;
        guint   continuity_counter;

        /* Section data (always newly allocated) */
        guint8 *section_data;
        /* Current offset in section_data */
        guint16 section_offset;

        /* Values for pending section */
        /* table_id of the pending section_data */
        guint8  table_id;
        guint   section_length;
        guint8  version_number;
        guint16 subtable_extension;
        guint8  section_number;
        guint8  last_section_number;

        GSList *subtables;

        /* Upstream offset of the data contained in the section */
        guint64 offset;

        /* Output data */
        PendingPacketState state;
} TSPacketStream;

/* PCR/offset structure */
typedef struct _PCROffset
{
        /* PCR value (units: 1/27MHz) */
        guint64 pcr;

        /* The offset (units: bytes) */
        guint64 offset;
} PCROffset;

/* PCROffsetGroup: A group of PCR observations.
 * All values in a group have got the same reference pcr and
 * byte offset (first_pcr/first_offset).
 */
#define DEFAULT_ALLOCATED_OFFSET 16
typedef struct _PCROffsetGroup
{
        /* Flags (see PCR_GROUP_FLAG_* above) */
        guint flags;

        /* First raw PCR of this group. Units: 1/27MHz.
         * All values[].pcr are differences against first_pcr */
        guint64 first_pcr;
        /* Offset of this group in bytes.
         * All values[].offset are differences against first_offset */
        guint64 first_offset;

        /* Dynamically allocated table of PCROffset */
        PCROffset *values;
        /* number of PCROffset allocated in values */
        guint nb_allocated;
        /* number of *actual* PCROffset contained in values */
        guint last_value;

        /* Offset since the very first PCR value observed in the whole
         * stream. Units: 1/27MHz.
         * This will take into account gaps/wraparounds/resets/... and is
         * used to determine running times.
         * The value is only guaranteed to be 100% accurate if the group
         * does not have the ESTIMATED flag.
         * If the value is estimated, the pcr_offset shall be recalculated
         * (based on previous groups) whenever it is accessed.
         */
        guint64 pcr_offset;

        /* FIXME : Cache group bitrate ? */
} PCROffsetGroup;

/* Number of PCRs needed before bitrate estimation can start */
/* Note: the reason we use 10 is because PCR should normally be
 * received at least every 100ms so this gives us close to
 * a 1s moving window to calculate bitrate */
#define PCR_BITRATE_NEEDED 10

/* PCROffsetCurrent: The PCR/Offset window iterator
 * This is used to estimate/observe incoming PCR/offset values
 * Points to a group (which it is filling) */
typedef struct _PCROffsetCurrent
{
        /* The PCROffsetGroup we are filling.
         * If NULL, a group needs to be identified */
        PCROffsetGroup *group;

        /* Table of pending values we are iterating over */
        PCROffset pending[PCR_BITRATE_NEEDED];

        /* base offset/pcr from the group */
        guint64 first_pcr;
        guint64 first_offset;

        /* The previous reference PCROffset
         * This corresponds to the last entry of the group we are filling
         * and is used to calculate prev_bitrate */
        PCROffset prev;

        /* The last PCROffset in pending[] */
        PCROffset last_value;

        /* Location of first pending PCR/offset observation in pending */
        guint first;
        /* Location of last pending PCR/offset observation in pending */
        guint last;
        /* Location of next write in pending */
        guint write;

        /* bitrate is always in bytes per second */

        /* cur_bitrate is the bitrate of the pending values: d(last-first) */
        guint64 cur_bitrate;

        /* prev_bitrate is the bitrate between reference PCROffset
         * and the first pending value. Used to detect changes
         * in bitrate */
        guint64 prev_bitrate;
} PCROffsetCurrent;

#define MAX_WINDOW 512

typedef struct _MpegTSPCR
{
        guint16 pid;

        /* Following variables are only active/used when
         * calculate_skew is TRUE */
        GstClockTime base_time;
        GstClockTime base_pcrtime;
        GstClockTime prev_out_time;
        GstClockTime prev_in_time;
        GstClockTime last_pcrtime;
        gint64 window[MAX_WINDOW];
        guint window_pos;
        guint window_size;
        gboolean window_filling;
        gint64 window_min;
        gint64 skew;
        gint64 prev_send_diff;

        /* Offset to apply to PCR to handle wraparounds */
        guint64 pcroffset;

        /* Used for bitrate calculation */
        /* List of PCR/offset observations */
        GList *groups;

        /* Current PCR/offset observations (used to update pcroffsets) */
        PCROffsetCurrent *current;
} MpegTSPCR;

typedef struct {
        gint16 pid;
        guint8 payload_unit_start_indicator;
        guint8 scram_afc_cc;
        const guint8 *payload;
        const guint8 *data_start;
        const guint8 *data_end;
        const guint8 *data;
        guint8 afc_flags;
        guint64 pcr;
        guint64 offset;
} TSPacket;

typedef struct
{
        guint8 table_id;
        /* the spec says sub_table_extension is the fourth and fifth byte of a 
         * section when the section_syntax_indicator is set to a value of "1". If 
         * section_syntax_indicator is 0, sub_table_extension will be set to 0 */
        guint16  subtable_extension;
        guint8   version_number;
        guint8   last_section_number;
        /* table of bits, whether the section was seen or not.
         * Use MPEGTS_BIT_* macros to check */
        /* Size is 32, because there's a maximum of 256 (32*8) section_number */
        guint8   seen_section[32];
} TSPacketStreamSubtable;

typedef struct _TSStream
{
        guint16             pid;
        guint8              stream_type;

        /* Content of the registration descriptor (if present) */
        guint32             registration_id;

        GstMpegTsPMTStream *stream;
} TSStream;


/* sync:4 == 00xx ! pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
#define READ_TS(data, target, lost_sync_label)          \
    if ((*data & 0x01) != 0x01) goto lost_sync_label;   \
    target  = ((guint64) (*data++ & 0x0E)) << 29;       \
    target |= ((guint64) (*data++       )) << 22;       \
    if ((*data & 0x01) != 0x01) goto lost_sync_label;   \
    target |= ((guint64) (*data++ & 0xFE)) << 14;       \
    target |= ((guint64) (*data++       )) << 7;        \
    if ((*data & 0x01) != 0x01) goto lost_sync_label;   \
    target |= ((guint64) (*data++ & 0xFE)) >> 1;

/*
 * PES stream_id assignments:
 *
 * 1011 1100                program_stream_map
 * 1011 1101                private_stream_1
 * 1011 1110                padding_stream
 * 1011 1111                private_stream_2
 * 110x xxxx                ISO/IEC 13818-3 or ISO/IEC 11172-3 audio stream number x xxxx
 * 1110 xxxx                ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 video stream number xxxx
 * 1111 0000                ECM_stream
 * 1111 0001                EMM_stream
 * 1111 0010                ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6_DSMCC_stream
 * 1111 0011                ISO/IEC_13522_stream
 * 1111 0100                ITU-T Rec. H.222.1 type A
 * 1111 0101                ITU-T Rec. H.222.1 type B
 * 1111 0110                ITU-T Rec. H.222.1 type C
 * 1111 0111                ITU-T Rec. H.222.1 type D
 * 1111 1000                ITU-T Rec. H.222.1 type E
 * 1111 1001                ancillary_stream
 * 1111 1010                ISO/IEC 14496-1_SL-packetized_stream
 * 1111 1011                ISO/IEC 14496-1_FlexMux_stream
 * 1111 1100                metadata stream
 * 1111 1101                extended_stream_id
 * 1111 1110                reserved data stream
 * 1111 1111                program_stream_directory
 */

#define ID_PS_END_CODE                          0xB9
#define ID_PS_PACK_START_CODE                   0xBA
#define ID_PS_SYSTEM_HEADER_START_CODE          0xBB
#define ID_PS_PROGRAM_STREAM_MAP                0xBC
#define ID_PRIVATE_STREAM_1                     0xBD
#define ID_PADDING_STREAM                       0xBE
#define ID_PRIVATE_STREAM_2                     0xBF
#define ID_ISO_IEC_MPEG12_AUDIO_STREAM_0        0xC0
#define ID_ISO_IEC_MPEG12_AUDIO_STREAM_32       0xDF
#define ID_ISO_IEC_MPEG12_VIDEO_STREAM_0        0xE0
#define ID_ISO_IEC_MPEG12_VIDEO_STREAM_16       0xEF
#define ID_ECM_STREAM                           0xF0
#define ID_EMM_STREAM                           0xF1
#define ID_DSMCC_STREAM                         0xF2
#define ID_ISO_IEC_13522_STREAM                 0xF3
#define ID_ITU_TREC_H222_TYPE_A_STREAM          0xF4
#define ID_ITU_TREC_H222_TYPE_B_STREAM          0xF5
#define ID_ITU_TREC_H222_TYPE_C_STREAM          0xF6
#define ID_ITU_TREC_H222_TYPE_D_STREAM          0xF7
#define ID_ITU_TREC_H222_TYPE_E_STREAM          0xF8
#define ID_ANCILLARY_STREAM                     0xF9
#define ID_14496_1_SL_PACKETIZED_STREAM         0xFA
#define ID_14496_1_SL_FLEXMUX_STREAM            0xFB
#define ID_METADATA_STREAM                      0xFC
#define ID_EXTENDED_STREAM_ID                   0xFD
#define ID_RESERVED_STREAM_3                    0xFE
#define ID_PROGRAM_STREAM_DIRECTORY             0xFF

/*
 * PES stream_id_extension assignments (if stream_id == ID_EXTENDED_STREAM_ID)
 *
 *  000 0000             IPMP Control Information stream
 *  000 0001             IPMP Stream
 *  000 0010 - 001 0001  ISO/IEC 14496-17 text Streams
 *  001 0010 - 010 0001  ISO/IEC 23002-3 auxiliary video data Streams
 *  ... .... - 011 1111  Reserved
 *
 *  PRIVATE STREAM RANGES (But known as used)
 *  101 0101 - 101 1111  VC-1
 *  110 0000 - 110 1111  Dirac (VC-1)
 *
 *  111 0001             AC3 or independent sub-stream 0 of EAC3/DD+
 *                       DTS or core sub-stream
 *  111 0010             dependent sub-stream of EAC3/DD+
 *                       DTS extension sub-stream
 *                       Secondary EAC3/DD+
 *                       Secondary DTS-HD LBR
 *  111 0110             AC3 in MLP/TrueHD
 *  1xx xxxx    private_stream
 */
#define EXT_ID_IPMP_CONTORL_INFORMATION_STREAM  0x00
#define EXT_ID_IPMP_STREAM                      0x01

/* VC-1 */
#define EXT_ID_VC1_FIRST                        0x55
#define EXT_ID_VC1_LAST                         0x5F

typedef enum {
  PES_FLAG_PRIORITY             = 1 << 3,       /* PES_priority (present: high-priority) */
  PES_FLAG_DATA_ALIGNMENT       = 1 << 2,       /* data_alignment_indicator */
  PES_FLAG_COPYRIGHT            = 1 << 1,       /* copyright */
  PES_FLAG_ORIGINAL_OR_COPY     = 1 << 0        /* original_or_copy */
} PESHeaderFlags;

typedef enum {
  PES_TRICK_MODE_FAST_FORWARD   = 0x000,
  PES_TRICK_MODE_SLOW_MOTION    = 0x001,
  PES_TRICK_MODE_FREEZE_FRAME   = 0x010,
  PES_TRICK_MODE_FAST_REVERSE   = 0x011,
  PES_TRICK_MODE_SLOW_REVERSE   = 0x100,
  /* ... */
  PES_TRICK_MODE_INVALID        = 0xfff /* Not present or invalid */
} PESTrickModeControl;

typedef enum {
  PES_FIELD_ID_TOP_ONLY         = 0x00, /* Display from top field only */
  PES_FIELD_ID_BOTTOM_ONLY      = 0x01, /* Display from bottom field only */
  PES_FIELD_ID_COMPLETE_FRAME   = 0x10, /* Display complete frame */
  PES_FIELD_ID_INVALID          = 0x11  /* Reserved/Invalid */
} PESFieldID;

typedef enum {
  PES_PARSING_OK        = 0,    /* Header fully parsed and valid */
  PES_PARSING_BAD       = 1,    /* Header invalid (CRC error for ex) */
  PES_PARSING_NEED_MORE = 2     /* Not enough data to parse header */
} PESParsingResult;

typedef struct {
  guint8        stream_id;      /* See ID_* above */
  guint32       packet_length;  /* The size of the PES header and PES data
                                 * (if 0 => unbounded packet) */
  guint16       header_size;    /* The complete size of the PES header */

  /* All remaining entries in this structure are optional */
  guint8        scrambling_control; /* 0x00  : Not scrambled/unspecified,
                                     * The following are according to ETSI TS 101 154
                                     * 0x01  : reserved for future DVB use
                                     * 0x10  : PES packet scrambled with Even key
                                     * 0x11  : PES packet scrambled with Odd key
                                     */
  PESHeaderFlags flags;

  guint64       PTS;            /* PTS (-1 if not present or invalid) */
  guint64       DTS;            /* DTS (-1 if not present or invalid) */
  guint64       ESCR;           /* ESCR (-1 if not present or invalid) */

  guint32       ES_rate;        /* in bytes/seconds (0 if not present or invalid) */
  PESTrickModeControl   trick_mode;
  
  /* Only valid for _FAST_FORWARD, _FAST_REVERSE and _FREEZE_FRAME */
  PESFieldID    field_id;
  /* Only valid for _FAST_FORWARD and _FAST_REVERSE */
  gboolean      intra_slice_refresh;
  guint8        frequency_truncation;
  /* Only valid for _SLOW_FORWARD and _SLOW_REVERSE */
  guint8        rep_cntrl;

  guint8        additional_copy_info; /* Private data */
  guint16       previous_PES_packet_CRC;

  /* Extension fields */
  const guint8* private_data;                   /* PES_private_data, 16 bytes long */
  guint8        pack_header_size;               /* Size of pack_header in bytes */
  const guint8* pack_header;
  gint8         program_packet_sequence_counter; /* -1 if not present or invalid */
  gboolean      MPEG1_MPEG2_identifier;
  guint8        original_stuff_length;

  guint32       P_STD_buffer_size; /* P-STD buffer size in bytes (0 if invalid
                                    * or not present */

  guint8        stream_id_extension; /* Public range (0x00 - 0x3f) only valid if stream_id == ID_EXTENDED_STREAM_ID
                                      * Private range (0x40 - 0xff) can be present in any stream type */

  gsize         extension_field_length;   /* Length of remaining extension field data */
  const guint8* stream_id_extension_data; /* Valid if extension_field_length != 0 */
} PESHeader;

/* MPEG_TO_GST calculation requires at least 17 extra bits (100000)
 * Since maximum PTS/DTS value is coded with 33bits, we are
 * safe to use direct calculation (17+33 < 63) */
#define MPEGTIME_TO_GSTTIME(t) ((t) * (guint64)100000 / 9)

typedef struct _TsSegment {
        GObject parent;

        GstElement element;
        GstPad *sinkpad, *srcpad;

        TSPacketStream **streams;

        /* Transport Stream segments MUST contain a single MPEG-2 Program;
         * playback of Multi-Program Transport Streams is not defined.  Each
         * Transport Stream segment SHOULD contain a PAT and a PMT at the start
         * of the segment - or have a Media Initialization Section declared in
         * the Media Playlist */
        guint program_number;
        guint16 pmt_pid;
        const GstMpegTsPMT *pmt;

        /* arrays that say whether a pid is a known psi pid or a pes pid */
        /* Use MPEGTS_BIT_* to set/unset/check the values */
        guint8 *known_psi;
        guint8 *is_pes;
        /* Whether we saw a PAT yet */
        gboolean seen_pat;
        gboolean seen_pmt;
        guint16 video_pid;
        /* Reference offset */
        //guint64 refoffset;
        GPtrArray *pat;
        /* whether we saw a key frame */
        gboolean seen_key_frame;
        guint8 pat_packet[188];
        guint8 pmt_packet[188];

        guint8 *data;
        /* Amount of bytes in current ->data */
        guint current_size;
        /* Size of ->data */
        guint allocated_size;
        /* Current PTS for the stream (in running time) */
        GstClockTime pts;
        GstClockTime duration;

        //gboolean push_section;

        /* current offset of the tip of the adapter */
        GstAdapter *adapter;
        guint64 offset;
        guint16 packet_size;
        const guint8 *map_data;
        gsize map_offset;
        gsize map_size;
        gboolean need_sync;

        GstH264NalParser *h264parser;
} TsSegment;

typedef struct _TsSegmentClass {
        GstElementClass parent_class;
} TsSegmentClass;

#define TYPE_TS_SEGMENT            (ts_segment_get_type())
#define TS_SEGMENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),TYPE_TS_SEGMENT,TsSegment))
#define TS_SEGMENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),TYPE_TS_SEGMENT,TsSegmentClass))
#define IS_TS_SEGMENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),TYPE_TS_SEGMENT))
#define IS_TS_SEGMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),TYPE_TS_SEGMENT))

GType ts_segment_get_type (void);

gboolean ts_segment_plugin_init (GstPlugin * plugin);

#endif /* __TSSEGMENT_H__ */