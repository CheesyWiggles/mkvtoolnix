/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   MP3 output module

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/codec.h"
#include "common/mp3.h"
#include "merge/connection_checks.h"
#include "merge/output_control.h"
#include "output/p_mp3.h"

using namespace libmatroska;

mp3_packetizer_c::mp3_packetizer_c(generic_reader_c *p_reader,
                                   track_info_c &p_ti,
                                   int samples_per_sec,
                                   int channels,
                                   bool source_is_good)
  : generic_packetizer_c(p_reader, p_ti)
  , m_first_packet{true}
  , m_bytes_skipped(0)
  , m_samples_per_sec(samples_per_sec)
  , m_channels(channels)
  , m_samples_per_frame(1152)
  , m_byte_buffer(128 * 1024)
  , m_codec_id_set(false)
  , m_valid_headers_found(source_is_good)
  , m_timecode_calculator{static_cast<int64_t>(samples_per_sec)}
  , m_packet_duration{m_timecode_calculator.get_duration(m_samples_per_frame).to_ns()}
{
  set_track_type(track_audio);
  set_track_default_duration(m_packet_duration);
}

mp3_packetizer_c::~mp3_packetizer_c() {
}

void
mp3_packetizer_c::handle_garbage(int64_t bytes) {
  bool warning_printed = false;

  if (m_first_packet) {
    auto offset = calculate_avi_audio_sync(bytes, m_samples_per_frame, m_packet_duration);

    if (0 < offset) {
      mxinfo_tid(m_ti.m_fname, m_ti.m_id,
                 boost::format(Y("This MPEG audio track contains %1% bytes of non-MP3 data at the beginning. "
                                 "This corresponds to a delay of %2%ms. This delay will be used instead of the garbage data.\n")) % bytes % (offset / 1000000));
      warning_printed             = true;
      m_ti.m_tcsync.displacement += offset;
    }
  }

  if (!warning_printed)
    mxwarn_tid(m_ti.m_fname, m_ti.m_id,
               boost::format(Y("This MPEG audio track contains %1% bytes of non-MP3 data which were skipped. "
                               "The audio/video synchronization may have been lost.\n")) % bytes);
}

unsigned char *
mp3_packetizer_c::get_mp3_packet(mp3_header_t *mp3header) {
  if (m_byte_buffer.get_size() == 0)
    return 0;

  int pos;
  size_t size;
  unsigned char *buf;

  while (1) {
    buf  = m_byte_buffer.get_buffer();
    size = m_byte_buffer.get_size();
    pos  = find_mp3_header(buf, size);

    if (0 > pos)
      return nullptr;

    decode_mp3_header(&buf[pos], mp3header);

    if ((pos + mp3header->framesize) > size)
      return nullptr;

    if (!mp3header->is_tag)
      break;

    mxverb(2, boost::format("mp3_packetizer: Removing TAG packet with size %1%\n") % mp3header->framesize);
    m_byte_buffer.remove(mp3header->framesize + pos);
  }

  // Try to be smart. We might get fed trash before the very first MP3
  // header. And now a user has presented streams in which the trash
  // contains valid MP3 headers before the 'real' ones...
  // Screw the guys who program apps that use _random_ _trash_ for filling
  // gaps. Screw those who try to use AVI no matter the 'cost'!
  bool track_headers_changed = false;
  if (!m_valid_headers_found) {
    pos = find_consecutive_mp3_headers(m_byte_buffer.get_buffer(), m_byte_buffer.get_size(), 5);
    if (0 > pos)
      return nullptr;

    // Great, we have found five consecutive identical headers. Be happy
    // with those!
    decode_mp3_header(m_byte_buffer.get_buffer() + pos, mp3header);

    set_audio_channels(mp3header->channels);
    set_audio_sampling_freq(mp3header->sampling_frequency);

    m_samples_per_sec      = mp3header->sampling_frequency;
    track_headers_changed  = true;
    m_valid_headers_found  = true;
    m_bytes_skipped       += pos;
    if (0 < m_bytes_skipped)
      handle_garbage(m_bytes_skipped);
    m_byte_buffer.remove(pos);

    pos             = 0;
    m_bytes_skipped = 0;
  }

  m_bytes_skipped += pos;
  if (0 < m_bytes_skipped)
    handle_garbage(m_bytes_skipped);
  m_byte_buffer.remove(pos);
  pos             = 0;
  m_bytes_skipped = 0;

  if (m_first_packet) {
    m_samples_per_frame  = mp3header->samples_per_channel;
    m_packet_duration    = m_timecode_calculator.get_duration(m_samples_per_frame).to_ns();
    std::string codec_id = MKV_A_MP3;
    codec_id[codec_id.length() - 1] = (char)(mp3header->layer + '0');
    set_codec_id(codec_id.c_str());

    m_timecode_calculator.set_samples_per_second(m_samples_per_sec);
    set_track_default_duration(m_packet_duration);

    track_headers_changed = true;
  }

  if (track_headers_changed)
    rerender_track_headers();

  if (mp3header->framesize > m_byte_buffer.get_size())
    return nullptr;

  buf = (unsigned char *)safememdup(m_byte_buffer.get_buffer(), mp3header->framesize);

  m_byte_buffer.remove(mp3header->framesize);

  return buf;
}

void
mp3_packetizer_c::set_headers() {
  if (!m_codec_id_set) {
    set_codec_id(MKV_A_MP3);
    m_codec_id_set = true;
  }
  set_audio_sampling_freq((float)m_samples_per_sec);
  set_audio_channels(m_channels);

  generic_packetizer_c::set_headers();
}

int
mp3_packetizer_c::process(packet_cptr packet) {
  m_timecode_calculator.add_timecode(packet);

  unsigned char *mp3_packet;
  mp3_header_t mp3header;

  m_byte_buffer.add(packet->data->get_buffer(), packet->data->get_size());

  while ((mp3_packet = get_mp3_packet(&mp3header))) {
    auto new_timecode = m_timecode_calculator.get_next_timecode(m_samples_per_frame);
    add_packet(std::make_shared<packet_t>(memory_c::clone(mp3_packet, mp3header.framesize), new_timecode.to_ns(), m_packet_duration));

    m_first_packet = false;
  }

  return FILE_STATUS_MOREDATA;
}

connection_result_e
mp3_packetizer_c::can_connect_to(generic_packetizer_c *src,
                                 std::string &error_message) {
  mp3_packetizer_c *msrc = dynamic_cast<mp3_packetizer_c *>(src);
  if (!msrc)
    return CAN_CONNECT_NO_FORMAT;

  connect_check_a_samplerate(m_samples_per_sec, msrc->m_samples_per_sec);
  connect_check_a_channels(m_channels, msrc->m_channels);

  return CAN_CONNECT_YES;
}
