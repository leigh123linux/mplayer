#ifndef __GUI_FS_H
#define __GUI_FS_H

#include <gtk/gtk.h>

#define fsVideoSelector    0
#define fsSubtitleSelector 1
#define fsOtherSelector    2
#define fsAudioSelector    3
#define fsFontSelector     4

#define fsPersistant_MaxPath    512
#define fsPersistant_MaxPos     10
#define fsPersistant_FilePath   "/.mplayer/phistory"

#include <errno.h>

extern GtkWidget   * fsFileSelect;

extern void HideFileSelect( void );
extern void ShowFileSelect( int type, int modal );

extern GtkWidget * create_FileSelect( void );

#endif
