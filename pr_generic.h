/*
  mkvmerge -- utility for splicing together matroska files
      from component media subtypes

  pr_generic.h

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file
    \version \$Id: pr_generic.h,v 1.15 2003/04/17 12:28:44 mosu Exp $
    \brief class definition for the generic reader and packetizer
    \author Moritz Bunkus         <moritz @ bunkus.org>
*/

#ifndef __PR_GENERIC_H
#define __PR_GENERIC_H

#include <stdio.h>

#include "IOCallback.h"
#include "KaxBlock.h"
#include "KaxCluster.h"
#include "KaxSegment.h"
#include "KaxTracks.h"

#include "common.h"

using namespace LIBMATROSKA_NAMESPACE;

extern KaxSegment kax_segment;
extern KaxTracks *kax_tracks;
extern KaxTrackEntry *kax_last_entry;
extern int track_number;

struct packet_t;

typedef struct {
  KaxCluster  *cluster;
  packet_t   **packets;
  int          num_packets, is_referenced, rendered;
} ch_contents_t;

class cluster_helper_c {
private:
  ch_contents_t **clusters;
  int             num_clusters, cluster_content_size;
  KaxBlockGroup  *last_block_group;
public:
  cluster_helper_c();
  virtual ~cluster_helper_c();

  void           add_cluster(KaxCluster *cluster);
  KaxCluster    *get_cluster();
  void           add_packet(packet_t *packet);
  int64_t        get_timecode();
  packet_t      *get_packet(int num);
  int            get_packet_count();
  int            render(IOCallback *out);
  int            free_ref(int64_t pid, void *source);
  int            free_clusters();
  int            get_cluster_content_size();

private:
  int            find_cluster(KaxCluster *cluster);
  ch_contents_t *find_packet_cluster(int64_t pid);
  packet_t      *find_packet(int64_t pid);
  void           free_contents(ch_contents_t *clstr);
  void           check_clusters(int num);
};

extern cluster_helper_c *cluster_helper;

class generic_packetizer_c {
protected:
  int serialno;
  track_info_t *ti;
public:
  KaxTrackEntry *track_entry;

  generic_packetizer_c(track_info_t *nti);
  virtual ~generic_packetizer_c();
  virtual int       packet_available() = 0;
  virtual packet_t *get_packet() = 0;
  virtual void      set_header() = 0;
  virtual stamp_t   get_smallest_timestamp() = 0;
  virtual int       process(unsigned char *data, int size,
                            int64_t timecode = -1, int64_t length = -1) = 0;
};
 
class generic_reader_c {
protected:
  track_info_t *ti;
public:
  generic_reader_c(track_info_t *nti);
  virtual ~generic_reader_c();
  virtual int       read() = 0;
  virtual packet_t *get_packet() = 0;
  virtual int       display_priority() = 0;
  virtual void      display_progress() = 0;
};

typedef struct packet_t {
  DataBuffer          *data_buffer;
  KaxBlockGroup       *group;
  KaxBlock            *block;
  KaxCluster          *cluster;
  unsigned char       *data;
  int                  length, superseeded, rendered;
  int64_t              timestamp, id, bref, fref;
  generic_packetizer_c *source;
} packet_t;

#endif  // __PR_GENERIC_H
