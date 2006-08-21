

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "config.h"
#include "mp_msg.h"
#include "help_mp.h"

#ifdef __FreeBSD__
#include <sys/cdrio.h>
#endif

#define FIRST_AC3_AID 128
#define FIRST_DTS_AID 136
#define FIRST_MPG_AID 0
#define FIRST_PCM_AID 160

#include "stream.h"
#include "m_option.h"
#include "m_struct.h"

#include "stream_dvd.h"

/// We keep these 2 for the gui atm, but they will be removed.
extern int dvd_title;
extern int dvd_chapter;
extern int dvd_last_chapter;
extern char* dvd_device;
int dvd_angle=1;

#ifdef USE_DVDREAD
#define	LIBDVDREAD_VERSION(maj,min,micro)	((maj)*10000 + (min)*100 + (micro))
/*
 * Try to autodetect the libdvd-0.9.0 library
 * (0.9.0 removed the <dvdread/dvd_udf.h> header, and moved the two defines
 * DVD_VIDEO_LB_LEN and MAX_UDF_FILE_NAME_LEN from it to
 * <dvdread/dvd_reader.h>)
 */
#ifndef DVDREAD_VERSION
#if defined(DVD_VIDEO_LB_LEN) && defined(MAX_UDF_FILE_NAME_LEN)
#define	DVDREAD_VERSION	LIBDVDREAD_VERSION(0,9,0)
#else
#define	DVDREAD_VERSION	LIBDVDREAD_VERSION(0,8,0)
#endif
#endif

char * dvd_audio_stream_types[8] = { "ac3","unknown","mpeg1","mpeg2ext","lpcm","unknown","dts" };
char * dvd_audio_stream_channels[6] = { "mono", "stereo", "unknown", "unknown", "5.1/6.1", "5.1" };
#endif /* #ifdef USE_DVDREAD */


static struct stream_priv_s {
  int title;
} stream_priv_dflts = {
  1
};

#define ST_OFF(f) M_ST_OFF(struct stream_priv_s,f)
/// URL definition
static m_option_t stream_opts_fields[] = {
  { "hostname", ST_OFF(title), CONF_TYPE_INT, M_OPT_MIN, 1, 0, NULL },
  { NULL, NULL, 0, 0, 0, 0,  NULL }
};
static struct m_struct_st stream_opts = {
  "dvd",
  sizeof(struct stream_priv_s),
  &stream_priv_dflts,
  stream_opts_fields
};

int dvd_parse_chapter_range(m_option_t *conf, const char *range) {
  const char *s;
  char *t;
  if (!range)
    return M_OPT_MISSING_PARAM;
  s = range;
  dvd_chapter = 1;
  dvd_last_chapter = 0;
  if(*range && isdigit(*range)) {
    dvd_chapter = strtol(range, &s, 10);
    if(range == s) {
      mp_msg(MSGT_OPEN, MSGL_ERR, MSGTR_DVDinvalidChapterRange, range);
      return M_OPT_INVALID;
    }
  }
  if(*s == 0)
    return 0;
  else if(*s != '-') {
    mp_msg(MSGT_OPEN, MSGL_ERR, MSGTR_DVDinvalidChapterRange, range);
    return M_OPT_INVALID;
  }
  ++s;
  if(*s == 0)
      return 0;
  if(! isdigit(*s)) {
    mp_msg(MSGT_OPEN, MSGL_ERR, MSGTR_DVDinvalidChapterRange, range);
    return M_OPT_INVALID;
  }
  dvd_last_chapter = strtol(s, &t, 10);
  if (s == t || *t) {
    mp_msg(MSGT_OPEN, MSGL_ERR, MSGTR_DVDinvalidChapterRange, range);
    return M_OPT_INVALID;
  }
  return 0;
}

#ifdef USE_DVDREAD
int dvd_chapter_from_cell(dvd_priv_t* dvd,int title,int cell)
{
  pgc_t * cur_pgc;
  ptt_info_t* ptt;
  int chapter = cell;
  int pgc_id,pgn;
  if(title < 0 || cell < 0){
    return 0;
  }
  /* for most DVD's chapter == cell */
  /* but there are more complecated cases... */
  if(chapter >= dvd->vmg_file->tt_srpt->title[title].nr_of_ptts) {
    chapter = dvd->vmg_file->tt_srpt->title[title].nr_of_ptts-1;
  }
  title = dvd->tt_srpt->title[title].vts_ttn-1;
  ptt = dvd->vts_file->vts_ptt_srpt->title[title].ptt;
  while(chapter >= 0) {
    pgc_id = ptt[chapter].pgcn;
    pgn = ptt[chapter].pgn;
    cur_pgc = dvd->vts_file->vts_pgcit->pgci_srp[pgc_id-1].pgc;
    if(cell >= cur_pgc->program_map[pgn-1]-1) {
      return chapter;
    }
    --chapter;
  }
  /* didn't find a chapter ??? */
  return chapter;
}

int dvd_aid_from_lang(stream_t *stream, unsigned char* lang) {
  dvd_priv_t *d=stream->priv;
  int code,i;
  if(lang) {
    while(strlen(lang)>=2) {
      code=lang[1]|(lang[0]<<8);
      for(i=0;i<d->nr_of_channels;i++) {
        if(d->audio_streams[i].language==code) {
          mp_msg(MSGT_OPEN,MSGL_INFO,MSGTR_DVDaudioChannel,
          d->audio_streams[i].id, lang[0],lang[1]);
          return d->audio_streams[i].id;
        }
        //printf("%X != %X  (%c%c)\n",code,d->audio_streams[i].language,lang[0],lang[1]);
      }
      lang+=2; while (lang[0]==',' || lang[0]==' ') ++lang;
    }
    mp_msg(MSGT_OPEN,MSGL_WARN,MSGTR_DVDnoMatchingAudio);
  }
  return -1;
}

int dvd_number_of_subs(stream_t *stream) {
  dvd_priv_t *d;
  if (!stream) return -1;
  d = stream->priv;
  if (!d) return -1;
  return d->nr_of_subtitles;
}

int dvd_lang_from_sid(stream_t *stream, int id) {
  dvd_priv_t *d;
  if (!stream) return 0;
  d = stream->priv;
  if (!d) return 0;
  if (id >= d->nr_of_subtitles) return 0;
  return d->subtitles[id].language;
}

int dvd_sid_from_lang(stream_t *stream, unsigned char* lang) {
  dvd_priv_t *d=stream->priv;
  int code,i;
  while(lang && strlen(lang)>=2) {
    code=lang[1]|(lang[0]<<8);
    for(i=0;i<d->nr_of_subtitles;i++) {
      if(d->subtitles[i].language==code) {
        mp_msg(MSGT_OPEN,MSGL_INFO,MSGTR_DVDsubtitleChannel, i, lang[0],lang[1]);
        return i;
      }
    }
    lang+=2; 
    while (lang[0]==',' || lang[0]==' ') ++lang;
  }
  mp_msg(MSGT_OPEN,MSGL_WARN,MSGTR_DVDnoMatchingSubtitle);
  return -1;
}

static int dvd_next_cell(dvd_priv_t *d) {
  int next_cell=d->cur_cell;

  mp_msg(MSGT_DVD,MSGL_DBG2, "dvd_next_cell: next1=0x%X  \n",next_cell);
  if( d->cur_pgc->cell_playback[ next_cell ].block_type == BLOCK_TYPE_ANGLE_BLOCK ) {
    while(next_cell<d->last_cell) {
      if( d->cur_pgc->cell_playback[next_cell].block_mode == BLOCK_MODE_LAST_CELL )
        break;
      ++next_cell;
    }
  }
  mp_msg(MSGT_DVD,MSGL_DBG2, "dvd_next_cell: next2=0x%X  \n",next_cell);

  ++next_cell;
  if(next_cell>=d->last_cell) 
    return -1; // EOF
  if(d->cur_pgc->cell_playback[next_cell].block_type == BLOCK_TYPE_ANGLE_BLOCK ) {
    next_cell+=dvd_angle;
    if(next_cell>=d->last_cell) 
      return -1; // EOF
  }
  mp_msg(MSGT_DVD,MSGL_DBG2, "dvd_next_cell: next3=0x%X  \n",next_cell);
  return next_cell;
}

int dvd_read_sector(dvd_priv_t *d,unsigned char* data) {
  int len;

  if(d->packs_left==0) {
    /**
     * If we're not at the end of this cell, we can determine the next
     * VOBU to display using the VOBU_SRI information section of the
     * DSI.  Using this value correctly follows the current angle,
     * avoiding the doubled scenes in The Matrix, and makes our life
     * really happy.
     *
     * Otherwise, we set our next address past the end of this cell to
     * force the code above to go to the next cell in the program.
     */
    if(d->dsi_pack.vobu_sri.next_vobu != SRI_END_OF_CELL) {
       d->cur_pack= d->dsi_pack.dsi_gi.nv_pck_lbn + ( d->dsi_pack.vobu_sri.next_vobu & 0x7fffffff );
       mp_msg(MSGT_DVD,MSGL_DBG2, "Navi  new pos=0x%X  \n",d->cur_pack);
    } else {
      // end of cell! find next cell!
      mp_msg(MSGT_DVD,MSGL_V, "--- END OF CELL !!! ---\n");
      d->cur_pack=d->cell_last_pack+1;
    }
  }

read_next:
  if(d->cur_pack>d->cell_last_pack) {
    // end of cell!
    int next=dvd_next_cell(d);
    if(next>=0) {
      d->cur_cell=next;
      // if( d->cur_pgc->cell_playback[d->cur_cell].block_type 
      // == BLOCK_TYPE_ANGLE_BLOCK ) d->cur_cell+=dvd_angle;
      d->cur_pack = d->cur_pgc->cell_playback[ d->cur_cell ].first_sector;
      d->cell_last_pack=d->cur_pgc->cell_playback[ d->cur_cell ].last_sector;
      mp_msg(MSGT_DVD,MSGL_V, "DVD next cell: %d  pack: 0x%X-0x%X  \n",d->cur_cell,d->cur_pack,d->cell_last_pack);
    } else 
        return -1; // EOF
  }

  len = DVDReadBlocks(d->title, d->cur_pack, 1, data);
  if(!len) return -1; //error

  if(data[38]==0 && data[39]==0 && data[40]==1 && data[41]==0xBF &&
    data[1024]==0 && data[1025]==0 && data[1026]==1 && data[1027]==0xBF) {
       // found a Navi packet!!!
#if DVDREAD_VERSION >= LIBDVDREAD_VERSION(0,9,0)
    navRead_DSI(&d->dsi_pack, &(data[ DSI_START_BYTE ]));
#else
    navRead_DSI(&d->dsi_pack, &(data[ DSI_START_BYTE ]), sizeof(dsi_t));
#endif
    if(d->cur_pack != d->dsi_pack.dsi_gi.nv_pck_lbn ) {
      mp_msg(MSGT_DVD,MSGL_V, "Invalid NAVI packet! lba=0x%X  navi=0x%X  \n",
        d->cur_pack,d->dsi_pack.dsi_gi.nv_pck_lbn);
    } else {
      // process!
      d->packs_left = d->dsi_pack.dsi_gi.vobu_ea;
      mp_msg(MSGT_DVD,MSGL_DBG2, "Found NAVI packet! lba=0x%X  len=%d  \n",d->cur_pack,d->packs_left);
      //navPrint_DSI(&d->dsi_pack);
      mp_msg(MSGT_DVD,MSGL_DBG3,"\r### CELL %d: Navi: %d/%d  IFO: %d/%d   \n",d->cur_cell,
        d->dsi_pack.dsi_gi.vobu_c_idn,d->dsi_pack.dsi_gi.vobu_vob_idn,
        d->cur_pgc->cell_position[d->cur_cell].cell_nr,
        d->cur_pgc->cell_position[d->cur_cell].vob_id_nr);

      if(d->angle_seek) {
        int i,skip=0;
#if defined(__GNUC__) && ( defined(__sparc__) || defined(hpux) )
        // workaround for a bug in the sparc/hpux version of gcc 2.95.X ... 3.2,
        // it generates incorrect code for unaligned access to a packed
        // structure member, resulting in an mplayer crash with a SIGBUS
        // signal.
        //
        // See also gcc problem report PR c/7847:
        // http://gcc.gnu.org/cgi-bin/gnatsweb.pl?database=gcc&cmd=view+audit-trail&pr=7847
        for(i=0;i<9;i++) {	// check if all values zero:
          typeof(d->dsi_pack.sml_agli.data[i].address) tmp_addr;
          memcpy(&tmp_addr,&d->dsi_pack.sml_agli.data[i].address,sizeof(tmp_addr));
          if((skip=tmp_addr)!=0) break;
        }
#else
        for(i=0;i<9;i++)	// check if all values zero:
          if((skip=d->dsi_pack.sml_agli.data[i].address)!=0) break;
#endif
        if(skip) {
          // sml_agli table has valid data (at least one non-zero):
         d->cur_pack=d->dsi_pack.dsi_gi.nv_pck_lbn+
         d->dsi_pack.sml_agli.data[dvd_angle].address;
         d->angle_seek=0;
         mp_msg(MSGT_DVD,MSGL_V, "Angle-seek synced using sml_agli map!  new_lba=0x%X  \n",d->cur_pack);
        } else {
          // check if we're in the right cell, jump otherwise:
          if( (d->dsi_pack.dsi_gi.vobu_c_idn==d->cur_pgc->cell_position[d->cur_cell].cell_nr) &&
            (d->dsi_pack.dsi_gi.vobu_vob_idn==d->cur_pgc->cell_position[d->cur_cell].vob_id_nr) ){
            d->angle_seek=0;
            mp_msg(MSGT_DVD,MSGL_V, "Angle-seek synced by cell/vob IDN search!  \n");
          } else {
            // wrong angle, skip this vobu:
            d->cur_pack=d->dsi_pack.dsi_gi.nv_pck_lbn+
            d->dsi_pack.dsi_gi.vobu_ea;
            d->angle_seek=2; // DEBUG
          }
        }
      }
    }
    ++d->cur_pack;
    goto read_next;
  }

  ++d->cur_pack;
  if(d->packs_left>=0) --d->packs_left;

  if(d->angle_seek) {
    if(d->angle_seek==2) mp_msg(MSGT_DVD,MSGL_V, "!!! warning! reading packet while angle_seek !!!\n");
    goto read_next; // searching for Navi packet
  }

  return d->cur_pack-1;
}

void dvd_seek(dvd_priv_t *d,int pos) {
  d->packs_left=-1;
  d->cur_pack=pos;

  // check if we stay in current cell (speedup things, and avoid angle skip)
  if(d->cur_pack>d->cell_last_pack ||
     d->cur_pack<d->cur_pgc->cell_playback[ d->cur_cell ].first_sector) {

    // ok, cell change, find the right cell!
    d->cur_cell=0;
    if(d->cur_pgc->cell_playback[d->cur_cell].block_type == BLOCK_TYPE_ANGLE_BLOCK )
      d->cur_cell+=dvd_angle;

    while(1) {
      int next;
      d->cell_last_pack=d->cur_pgc->cell_playback[ d->cur_cell ].last_sector;
      if(d->cur_pack<d->cur_pgc->cell_playback[ d->cur_cell ].first_sector) {
        d->cur_pack=d->cur_pgc->cell_playback[ d->cur_cell ].first_sector;
        break;
      }
      if(d->cur_pack<=d->cell_last_pack) break; // ok, we find it! :)
      next=dvd_next_cell(d);
      if(next<0) {
        //d->cur_pack=d->cell_last_pack+1;
        break; // we're after the last cell
      }
      d->cur_cell=next;
    }
  }

  mp_msg(MSGT_DVD,MSGL_V, "DVD Seek! lba=0x%X  cell=%d  packs: 0x%X-0x%X  \n",
    d->cur_pack,d->cur_cell,d->cur_pgc->cell_playback[ d->cur_cell ].first_sector,d->cell_last_pack);

  // if we're in interleaved multi-angle cell, find the right angle chain!
  // (read Navi block, and use the seamless angle jump table)
  d->angle_seek=1;
}

void dvd_close(dvd_priv_t *d) {
  ifoClose(d->vts_file);
  ifoClose(d->vmg_file);
  DVDCloseFile(d->title);
  DVDClose(d->dvd);
  dvd_chapter = 1;
  dvd_last_chapter = 0;
}

#endif /* #ifdef USE_DVDREAD */

static int fill_buffer(stream_t *s, char *but, int len)
{
#ifdef USE_DVDREAD
  if(s->type == STREAMTYPE_DVD) {
    off_t pos=dvd_read_sector(s->priv,s->buffer);
    if(pos>=0) {
      len=2048; // full sector
      s->pos=2048*pos-len;
    } else len=-1; // error
  }
#endif
  return len;
}

static int seek(stream_t *s, off_t newpos) {
#ifdef USE_DVDREAD
  s->pos=newpos; // real seek
  dvd_seek(s->priv,s->pos/2048);
#endif
  return 1;
}

static void stream_dvd_close(stream_t *s) {
#ifdef USE_DVDREAD
  dvd_close(s->priv);
#endif
}

/** 
\brief Converts DVD time structure to milliseconds.
\param *dev the DVD time structure to convert
\return returns the time in milliseconds
*/
static int dvdtimetomsec(dvd_time_t *dt)
{
  static int framerates[4] = {0, 2500, 0, 2997};
  int framerate = framerates[(dt->frame_u & 0xc0) >> 6];
  int msec = (((dt->hour & 0xf0) >> 3) * 5 + (dt->hour & 0x0f)) * 3600000;
  msec += (((dt->minute & 0xf0) >> 3) * 5 + (dt->minute & 0x0f)) * 60000;
  msec += (((dt->second & 0xf0) >> 3) * 5 + (dt->second & 0x0f)) * 1000;
  if(framerate > 0)
    msec += (((dt->frame_u & 0x30) >> 3) * 5 + (dt->frame_u & 0x0f)) * 100000 / framerate;
  return msec;
}

static int mp_get_titleset_length(ifo_handle_t *vts_file, tt_srpt_t *tt_srpt, int title_no)
{
    int vts_ttn;  ///< title number within video title set
    int pgc_no;   ///< program chain number
    int msec;     ///< time length in milliseconds

    msec=0;
    if(!vts_file || !tt_srpt)
        return 0;

    if(vts_file->vtsi_mat && vts_file->vts_pgcit)
    {
            vts_ttn = tt_srpt->title[title_no].vts_ttn - 1;
            pgc_no = vts_file->vts_ptt_srpt->title[vts_ttn].ptt[0].pgcn - 1;
            msec = dvdtimetomsec(&vts_file->vts_pgcit->pgci_srp[pgc_no].pgc->playback_time);
    }
    return msec;
}


static int mp_describe_titleset(dvd_reader_t *dvd, tt_srpt_t *tt_srpt, int vts_no)
{
    ifo_handle_t *vts_file;
    int title_no, msec=0;

    vts_file = ifoOpen(dvd, vts_no);
    if(!vts_file)
        return 0;

    if(!vts_file->vtsi_mat || !vts_file->vts_pgcit)
    {
        ifoClose(vts_file);
        return 0;
    }

    for(title_no = 0; title_no < tt_srpt->nr_of_srpts; title_no++)
    {
        if (tt_srpt->title[title_no].title_set_nr != vts_no)
            continue;
        msec = mp_get_titleset_length(vts_file, tt_srpt, title_no);
        mp_msg(MSGT_IDENTIFY, MSGL_V, "ID_DVD_TITLE_%d_LENGTH=%d.%03d\n", title_no + 1, msec / 1000, msec % 1000);
    }
    ifoClose(vts_file);
    return 1;
}

static int seek_to_chapter(stream_t *stream, ifo_handle_t *vts_file, tt_srpt_t *tt_srpt, int title_no, int chapter)
{
    int cell;
    ptt_info_t ptt;
    pgc_t *pgc;
    off_t pos;

    if(!vts_file || !tt_srpt)
       return 0;

    if(chapter < 0 || chapter > vts_file->vts_ptt_srpt->title[title_no].nr_of_ptts-1) //no such chapter
       return 0;

    ptt = vts_file->vts_ptt_srpt->title[title_no].ptt[chapter];
    pgc = vts_file->vts_pgcit->pgci_srp[ptt.pgcn-1].pgc;

    cell = pgc->program_map[ptt.pgn - 1] - 1;
    pos = (off_t) pgc->cell_playback[cell].first_sector * 2048;
    mp_msg(MSGT_OPEN,MSGL_V,"\r\nSTREAM_DVD, seeked to chapter: %d, cell: %u, pos: %"PRIu64"\n",
        chapter, pgc->cell_playback[cell].first_sector, pos);
    stream_seek(stream, pos);

    return chapter;
}

static int control(stream_t *stream,int cmd,void* arg) 
{
    dvd_priv_t *d = stream->priv;
    switch(cmd) 
    {
        case STREAM_CTRL_GET_TIME_LENGTH:
        {
            *((unsigned int *)arg) = mp_get_titleset_length(d->vts_file, d->tt_srpt, d->cur_title-1);
            return 1;
        }
        case STREAM_CTRL_GET_NUM_CHAPTERS:
        {
            if(! d->cur_pgc->nr_of_programs) return STREAM_UNSUPORTED;
            *((unsigned int *)arg) = d->cur_pgc->nr_of_programs; 
            return 1;
        }
        case STREAM_CTRL_SEEK_TO_CHAPTER:
        {
            int r = seek_to_chapter(stream, d->vts_file, d->tt_srpt, d->cur_title-1, *((unsigned int *)arg));
            if(! r) return STREAM_UNSUPORTED;

            return 1;
        }
        case STREAM_CTRL_GET_CURRENT_CHAPTER:
        {
            *((unsigned int *)arg) = dvd_chapter_from_cell(d, d->cur_title-1, d->cur_cell);
            return 1;
        }
    }
    return STREAM_UNSUPORTED;
}


static int open_s(stream_t *stream,int mode, void* opts, int* file_format) {
  struct stream_priv_s* p = (struct stream_priv_s*)opts;
  char *filename;

  filename = strdup(stream->url);
  mp_msg(MSGT_OPEN,MSGL_V,"URL: %s\n", filename);
  dvd_title = p->title;
#ifdef USE_DVDREAD
  if(1){
    //int ret,ret2;
    dvd_priv_t *d;
    int ttn,pgc_id,pgn;
    dvd_reader_t *dvd;
    dvd_file_t *title;
    ifo_handle_t *vmg_file;
    tt_srpt_t *tt_srpt;
    ifo_handle_t *vts_file;
    /**
     * Open the disc.
     */
    if(!dvd_device) dvd_device=strdup(DEFAULT_DVD_DEVICE);
#ifdef SYS_DARWIN
    /* Dynamic DVD drive selection on Darwin */
    if(!strcmp(dvd_device, "/dev/rdiskN")) {
      int i;
      char *temp_device = malloc(strlen(dvd_device)+1);

      for (i = 1; i < 10; i++) {
        sprintf(temp_device, "/dev/rdisk%d", i);
        dvd = DVDOpen(temp_device);
        if(!dvd) {
          mp_msg(MSGT_OPEN,MSGL_ERR,MSGTR_CantOpenDVD,temp_device);
        } else {
#if DVDREAD_VERSION <= LIBDVDREAD_VERSION(0,9,4)
          int len;
          if(!UDFFindFile(dvd,"/",&len)) {
            mp_msg(MSGT_OPEN,MSGL_ERR,MSGTR_CantOpenDVD,temp_device);
            DVDClose(dvd);
          } else
#endif
          {
          free(temp_device);
          break;
          }
        }
      }

      if(!dvd) {
        m_struct_free(&stream_opts,opts);
        return STREAM_UNSUPORTED;
      }
    } else
#endif /* SYS_DARWIN */
    {
        dvd = DVDOpen(dvd_device);
        if(!dvd) {
          mp_msg(MSGT_OPEN,MSGL_ERR,MSGTR_CantOpenDVD,dvd_device);
          m_struct_free(&stream_opts,opts);
          return STREAM_UNSUPORTED;
        }
    }

    mp_msg(MSGT_OPEN,MSGL_V,"Reading disc structure, please wait...\n");

    /**
     * Load the video manager to find out the information about the titles on
     * this disc.
     */
    vmg_file = ifoOpen(dvd, 0);
    if(!vmg_file) {
      mp_msg(MSGT_OPEN,MSGL_ERR, MSGTR_DVDnoVMG);
      DVDClose( dvd );
      m_struct_free(&stream_opts,opts);
      return STREAM_UNSUPORTED;
    }
    tt_srpt = vmg_file->tt_srpt;
    if (mp_msg_test(MSGT_IDENTIFY, MSGL_INFO))
    {
      int title_no; ///< title number
      mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_DVD_TITLES=%d\n", tt_srpt->nr_of_srpts);
      for (title_no = 0; title_no < tt_srpt->nr_of_srpts; title_no++)
      {
        mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_DVD_TITLE_%d_CHAPTERS=%d\n", title_no + 1, tt_srpt->title[title_no].nr_of_ptts);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_DVD_TITLE_%d_ANGLES=%d\n", title_no + 1, tt_srpt->title[title_no].nr_of_angles);
      }
    }
    if (mp_msg_test(MSGT_IDENTIFY, MSGL_V))
    {
      unsigned char discid [16]; ///< disk ID, a 128 bit MD5 sum
      int vts_no;   ///< video title set number
      for (vts_no = 1; vts_no <= vmg_file->vts_atrt->nr_of_vtss; vts_no++)
        mp_describe_titleset(dvd, tt_srpt, vts_no);
      if (DVDDiscID(dvd, discid) >= 0)
      {
        int i;
        char buf[33];
        for (i = 0; i < 16; i ++)
          sprintf(buf+2*i, "%02X", discid[i]);
        mp_msg(MSGT_IDENTIFY, MSGL_V, "ID_DVD_DISC_ID=%s\n", buf);
      }
    }
    /**
     * Make sure our title number is valid.
     */
    mp_msg(MSGT_OPEN,MSGL_STATUS, MSGTR_DVDnumTitles, tt_srpt->nr_of_srpts );
    if(dvd_title < 1 || dvd_title > tt_srpt->nr_of_srpts) {
      mp_msg(MSGT_OPEN,MSGL_ERR, MSGTR_DVDinvalidTitle, dvd_title);
      ifoClose( vmg_file );
      DVDClose( dvd );
      m_struct_free(&stream_opts,opts);
      return STREAM_UNSUPORTED;
    }
    --dvd_title; // remap 1.. -> 0..
    /**
     * Make sure the chapter number is valid for this title.
     */
    mp_msg(MSGT_OPEN,MSGL_STATUS, MSGTR_DVDnumChapters, tt_srpt->title[dvd_title].nr_of_ptts);
    if(dvd_chapter<1 || dvd_chapter>tt_srpt->title[dvd_title].nr_of_ptts) {
      mp_msg(MSGT_OPEN,MSGL_ERR, MSGTR_DVDinvalidChapter, dvd_chapter);
      ifoClose( vmg_file );
      DVDClose( dvd );
      m_struct_free(&stream_opts,opts);
      return STREAM_UNSUPORTED;
    }
    if(dvd_last_chapter>0) {
      if(dvd_last_chapter<dvd_chapter || dvd_last_chapter>tt_srpt->title[dvd_title].nr_of_ptts) {
        mp_msg(MSGT_OPEN,MSGL_ERR, MSGTR_DVDinvalidLastChapter, dvd_last_chapter);
        ifoClose( vmg_file );
        DVDClose( dvd );
        m_struct_free(&stream_opts,opts);
        return STREAM_UNSUPORTED;
      }
    }
    --dvd_chapter; // remap 1.. -> 0..
    /* XXX No need to remap dvd_last_chapter */
    /**
     * Make sure the angle number is valid for this title.
     */
    mp_msg(MSGT_OPEN,MSGL_STATUS, MSGTR_DVDnumAngles, tt_srpt->title[dvd_title].nr_of_angles);
    if(dvd_angle<1 || dvd_angle>tt_srpt->title[dvd_title].nr_of_angles) {
      mp_msg(MSGT_OPEN,MSGL_ERR, MSGTR_DVDinvalidAngle, dvd_angle);
      ifoClose( vmg_file );
      DVDClose( dvd );
      m_struct_free(&stream_opts,opts);
      return STREAM_UNSUPORTED;
    }
    --dvd_angle; // remap 1.. -> 0..

    ttn = tt_srpt->title[dvd_title].vts_ttn - 1;
    /**
     * Load the VTS information for the title set our title is in.
     */
    vts_file = ifoOpen( dvd, tt_srpt->title[dvd_title].title_set_nr );
    if(!vts_file) {
      mp_msg(MSGT_OPEN,MSGL_ERR, MSGTR_DVDnoIFO, tt_srpt->title[dvd_title].title_set_nr );
      ifoClose( vmg_file );
      DVDClose( dvd );
      m_struct_free(&stream_opts,opts);
      return STREAM_UNSUPORTED;
    }
    /**
     * We've got enough info, time to open the title set data.
     */
    title = DVDOpenFile(dvd, tt_srpt->title[dvd_title].title_set_nr, DVD_READ_TITLE_VOBS);
    if(!title) {
      mp_msg(MSGT_OPEN,MSGL_ERR, MSGTR_DVDnoVOBs, tt_srpt->title[dvd_title].title_set_nr);
      ifoClose( vts_file );
      ifoClose( vmg_file );
      DVDClose( dvd );
      m_struct_free(&stream_opts,opts);
      return STREAM_UNSUPORTED;
    }

    mp_msg(MSGT_OPEN,MSGL_V, "DVD successfully opened.\n");
    // store data
    d=malloc(sizeof(dvd_priv_t)); memset(d,0,sizeof(dvd_priv_t));
    d->dvd=dvd;
    d->title=title;
    d->vmg_file=vmg_file;
    d->tt_srpt=tt_srpt;
    d->vts_file=vts_file;
    d->cur_title = dvd_title+1;

    /**
     * Check number of audio channels and types
     */
    {
      d->nr_of_channels=0;
      if(vts_file->vts_pgcit) {
        int i;
        for(i=0;i<8;i++)
#ifdef USE_MPDVDKIT
          if(vts_file->vts_pgcit->pgci_srp[ttn].pgc->audio_control[i].present) {
#else
          if(vts_file->vts_pgcit->pgci_srp[ttn].pgc->audio_control[i] & 0x8000) {
#endif
            audio_attr_t * audio = &vts_file->vtsi_mat->vts_audio_attr[i];
            int language = 0;
            char tmp[] = "unknown";

            if(audio->lang_type == 1) {
              language=audio->lang_code;
              tmp[0]=language>>8;
              tmp[1]=language&0xff;
              tmp[2]=0;
            }

            d->audio_streams[d->nr_of_channels].language=language;
#ifdef USE_MPDVDKIT
            d->audio_streams[d->nr_of_channels].id=vts_file->vts_pgcit->pgci_srp[ttn].pgc->audio_control[i].s_audio;
#else
            d->audio_streams[d->nr_of_channels].id=vts_file->vts_pgcit->pgci_srp[ttn].pgc->audio_control[i] >> 8 & 7;
#endif
            switch(audio->audio_format) {
              case 0: // ac3
                d->audio_streams[d->nr_of_channels].id+=FIRST_AC3_AID;
                break;
              case 6: // dts
                d->audio_streams[d->nr_of_channels].id+=FIRST_DTS_AID;
                break;
              case 2: // mpeg layer 1/2/3
              case 3: // mpeg2 ext
                d->audio_streams[d->nr_of_channels].id+=FIRST_MPG_AID;
                break;
              case 4: // lpcm
                d->audio_streams[d->nr_of_channels].id+=FIRST_PCM_AID;
                break;
           }

           d->audio_streams[d->nr_of_channels].type=audio->audio_format;
           // Pontscho: to my mind, tha channels:
           //  1 - stereo
           //  5 - 5.1
           d->audio_streams[d->nr_of_channels].channels=audio->channels;
           mp_msg(MSGT_OPEN,MSGL_STATUS,MSGTR_DVDaudioStreamInfo,
             d->nr_of_channels,
             dvd_audio_stream_types[ audio->audio_format ],
             dvd_audio_stream_channels[ audio->channels ],
             tmp,
             d->audio_streams[d->nr_of_channels].id
           );
           mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_AUDIO_ID=%d\n", d->audio_streams[d->nr_of_channels].id);
           if(language && tmp[0])
             mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_AID_%d_LANG=%s\n", d->audio_streams[d->nr_of_channels].id, tmp);

           d->nr_of_channels++;
         }
      }
      mp_msg(MSGT_OPEN,MSGL_STATUS,MSGTR_DVDnumAudioChannels,d->nr_of_channels );
    }

    /**
     * Check number of subtitles and language
     */
    {
      int i;

      d->nr_of_subtitles=0;
      for(i=0;i<32;i++)
#ifdef USE_MPDVDKIT
      if(vts_file->vts_pgcit->pgci_srp[ttn].pgc->subp_control[i].present) {
#else
      if(vts_file->vts_pgcit->pgci_srp[ttn].pgc->subp_control[i] & 0x80000000) {
#endif
        subp_attr_t * subtitle = &vts_file->vtsi_mat->vts_subp_attr[i];
        video_attr_t *video = &vts_file->vtsi_mat->vts_video_attr;
        int language = 0;
        char tmp[] = "unknown";

        if(subtitle->type == 1) {
          language=subtitle->lang_code;
          tmp[0]=language>>8;
          tmp[1]=language&0xff;
          tmp[2]=0;
        }

        d->subtitles[ d->nr_of_subtitles ].language=language;
        d->subtitles[ d->nr_of_subtitles ].id=d->nr_of_subtitles;
        if(video->display_aspect_ratio == 0) /* 4:3 */
#ifdef USE_MPDVDKIT
          d->subtitles[d->nr_of_subtitles].id = vts_file->vts_pgcit->pgci_srp[ttn].pgc->subp_control[i].s_4p3;
#else
          d->subtitles[d->nr_of_subtitles].id = vts_file->vts_pgcit->pgci_srp[ttn].pgc->subp_control[i] >> 24 & 31;
#endif
        else if(video->display_aspect_ratio == 3) /* 16:9 */
#ifdef USE_MPDVDKIT
          d->subtitles[d->nr_of_subtitles].id = vts_file->vts_pgcit->pgci_srp[ttn].pgc->subp_control[i].s_lbox;
#else
          d->subtitles[d->nr_of_subtitles].id = vts_file->vts_pgcit->pgci_srp[ttn].pgc->subp_control[i] >> 8 & 31;
#endif

        mp_msg(MSGT_OPEN,MSGL_STATUS,MSGTR_DVDsubtitleLanguage, d->nr_of_subtitles, tmp);
        mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_SUBTITLE_ID=%d\n", d->subtitles[d->nr_of_subtitles].id);
        if(language && tmp[0])
          mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_SID_%d_LANG=%s\n", d->nr_of_subtitles, tmp);
        d->nr_of_subtitles++;
      }
      mp_msg(MSGT_OPEN,MSGL_STATUS,MSGTR_DVDnumSubtitles,d->nr_of_subtitles);
    }

    /**
     * Determine which program chain we want to watch.  This is based on the
     * chapter number.
     */
    pgc_id = vts_file->vts_ptt_srpt->title[ttn].ptt[dvd_chapter].pgcn; // local
    pgn  = vts_file->vts_ptt_srpt->title[ttn].ptt[dvd_chapter].pgn;  // local
    d->cur_pgc = vts_file->vts_pgcit->pgci_srp[pgc_id-1].pgc;
    d->cur_cell = d->cur_pgc->program_map[pgn-1] - 1; // start playback here
    d->packs_left=-1;      // for Navi stuff
    d->angle_seek=0;
    /* XXX dvd_last_chapter is in the range 1..nr_of_ptts */
    if(dvd_last_chapter > 0 && dvd_last_chapter < tt_srpt->title[dvd_title].nr_of_ptts) {
      pgn=vts_file->vts_ptt_srpt->title[ttn].ptt[dvd_last_chapter].pgn;
      d->last_cell=d->cur_pgc->program_map[pgn-1] - 1;
    } else
      d->last_cell=d->cur_pgc->nr_of_cells;

    if(d->cur_pgc->cell_playback[d->cur_cell].block_type == BLOCK_TYPE_ANGLE_BLOCK ) 
      d->cur_cell+=dvd_angle;
    d->cur_pack = d->cur_pgc->cell_playback[ d->cur_cell ].first_sector;
    d->cell_last_pack=d->cur_pgc->cell_playback[ d->cur_cell ].last_sector;
    mp_msg(MSGT_DVD,MSGL_V, "DVD start cell: %d  pack: 0x%X-0x%X  \n",d->cur_cell,d->cur_pack,d->cell_last_pack);

    // ... (unimplemented)
    //    return NULL;
    stream->type = STREAMTYPE_DVD;
    stream->sector_size = 2048;
    stream->flags = STREAM_READ | STREAM_SEEK;
    stream->fill_buffer = fill_buffer;
    stream->seek = seek;
    stream->control = control;
    stream->close = stream_dvd_close;
    stream->start_pos = (off_t)d->cur_pack*2048;
    stream->end_pos = (off_t)(d->cur_pgc->cell_playback[d->last_cell-1].last_sector)*2048;
    mp_msg(MSGT_DVD,MSGL_V,"DVD start=%d end=%d  \n",d->cur_pack,d->cur_pgc->cell_playback[d->last_cell-1].last_sector);
    stream->priv = (void*)d;
    return STREAM_OK;
  }
#endif /* #ifdef USE_DVDREAD */
  mp_msg(MSGT_DVD,MSGL_ERR,MSGTR_NoDVDSupport);
  m_struct_free(&stream_opts,opts);
  return STREAM_UNSUPORTED;
}


stream_info_t stream_info_dvd = {
  "DVD stream",
  "null",
  "",
  "",
  open_s,
  { "dvd", NULL },
  &stream_opts,
  1 // Urls are an option string
};
