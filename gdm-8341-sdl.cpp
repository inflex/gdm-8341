/*
 * GwInstek GDM-8341
 *
 * December 29, 2020
 *
 * Written by Paul L Daniels (pldaniels@gmail.com)
 *
 */

#include <SDL.h>
#include <SDL_ttf.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>

#define FL __FILE__,__LINE__

/*
 * Should be defined in the Makefile to pass to the compiler from
 * the github build revision
 *
 */
#ifndef BUILD_VER 
#define BUILD_VER 000
#endif

#ifndef BUILD_DATE
#define BUILD_DATE " "
#endif

#define SSIZE 1024

#define INTERFRAME_SLEEP	200000 // 0.2 seconds

#define ee ""
#define uu "\u00B5"
#define kk "k"
#define MM "M"
#define mm "m"
#define nn "n"
#define pp "p"
#define dd "\u00B0"
#define oo "\u03A9"

#define CMODE_USB 1
#define CMODE_SERIAL 2
#define CMODE_NONE 0

struct mmode_s {
	char scpi[50];
	char label[50];
	char query[50];
	char units[10];
};

#define MMODES_VOLT_DC 0
#define MMODES_VOLT_AC 1
#define MMODES_VOLT_DCAC 2
#define MMODES_CURR_DC 3
#define MMODES_CURR_AC 4
#define MMODES_CURR_DCAC 5
#define MMODES_RES 6
#define MMODES_FREQ 7
#define MMODES_PER 8
#define MMODES_TEMP 9
#define MMODES_DIOD 10
#define MMODES_CONT 11
#define MMODES_CAP 12
#define MMODES_MAX 13

struct mmode_s mmodes[] = { 
	{"VOLT", "Volts DC", "MEAS:VOLT:DC?\r\n", "V DC"}, 
	{"VOLT:AC", "Volts AC", "MEAS:VOLT:AC?\r\n", "V AC"},
	{"VOLT:DCAC", "Volts DC/AC", "MEAS:VOLT:DCAC?\r\n", "V DC/AC"},
	{"CURR", "Current DC", "MEAS:CURR:DC?\r\n", "A DC"},
	{"CURR:AC", "Current AC", "MEAS:CURR:AC?\r\n", "A AC"},
	{"CURR:DCAC", "Current DC/AC", "MEAS:CURR:DCAC?\r\n", "A DC/AC"},
	{"RES", "Resistance", "MEAS:RES?\r\n", oo },
	{"FREQ", "Frequency", "MEAS:FREQ?\r\n", "Hz" },
	{"PER", "Period", "MEAS:PER?\r\n", "s"},
	{"TEMP", "Temperature", "MEAS:TEMP:TCO?\r\n", "C"},
	{"DIOD", "Diode", "MEAS:DIOD?\r\n", "V"},
	{"CONT", "Continuity", "MEAS:CONT?\r\n", oo},
	{"CAP", "Capacitance", "MEAS:CAP?\r\n", "F"}
};

const char SCPI_FUNC[] = "SENS:FUNC1?\r\n";
const char SCPI_VAL[] = "VAL1?\r\n";
const char SCPI_VAL2[] = "VAL2?\r\n";
const char SCPI_CONT_THRESHOLD[] = "SENS:CONT:THR?\r\n";
const char SCPI_LOCAL[] = "SYST:LOC\r\n";
const char SCPI_RANGE[] = "CONF:RANG?\r\n";

const char SEPARATOR_DP[] = ".";

struct serial_params_s {
	char *device;
	int fd, n;
	int cnt, size, s_cnt;
	struct termios oldtp, newtp;
};


struct glb {
	uint8_t debug;
	uint8_t quiet;
	uint16_t flags;
	uint16_t error_flag;
	char *output_file;
	char *device;

	int usb_fhandle;

	int comms_mode;
	char *com_address;
	char *serial_parameters_string; // this is the raw from the command line
	struct serial_params_s serial_params; // this is the decoded version


	int interval;
	int font_size;
	int window_width, window_height;
	int wx_forced, wy_forced;
	SDL_Color font_color_pri, font_color_sec, background_color;
};

/*
 * A whole bunch of globals, because I need
 * them accessible in the Windows handler
 *
 * So many of these I'd like to try get away from being
 * a global.
 *
 */
struct glb *glbs;

/*
 * Test to see if a file exists
 *
 * Does not test anything else such as read/write status
 *
 */
bool fileExists(const char *filename) {
	struct stat buf;
	return (stat(filename, &buf) == 0);
}


/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220248
  Function Name	: init
  Returns Type	: int
  ----Parameter List
  1. struct glb *g ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int init(struct glb *g) {
	g->debug = 0;
	g->quiet = 0;
	g->flags = 0;
	g->error_flag = 0;
	g->output_file = NULL;
	g->interval = 10000;
	g->device = NULL;
	g->comms_mode = CMODE_NONE;

	g->serial_parameters_string = NULL;

	g->font_size = 60;
	g->window_width = 400;
	g->window_height = 100;
	g->wx_forced = 0;
	g->wy_forced = 0;

	g->font_color_pri =  { 10, 200, 10 };
	g->font_color_sec =  { 200, 200, 10 };
	g->background_color = { 0, 0, 0 };

	return 0;
}

void show_help(void) {
	fprintf(stdout,"gdm-8341 Power supply display\r\n"
			"By Paul L Daniels / pldaniels@gmail.com\r\n"
			"Build %d / %s\r\n"
			"\r\n"
			" [-p <usbtmc path, ie /dev/usbtmc2>] \r\n"
			"\r\n"
			"\t-h: This help\r\n"
			"\t-d: debug enabled\r\n"
			"\t-q: quiet output\r\n"
			"\t-v: show version\r\n"
			"\t-z <font size in pt>\r\n"
			"\t-cv <volts colour, a0a0ff>\r\n"
			"\t-ca <amps colour, ffffa0>\r\n"
			"\t-cb <background colour, 101010>\r\n"
			"\t-t <interval> (sleep delay between samples, default 100,000us)\r\n"
			"\t-p <comport>: Set the com port for the meter, eg: -p /dev/ttyUSB0\r\n"
			"\t-s <115200|57600|38400|19200|9600> serial speed (default 115200)\r\n"
			"\r\n"
			"\texample: gdm-8341-sdl -p /dev/ttyUSB0 -s 115200\r\n"
			, BUILD_VER
			, BUILD_DATE 
			);
} 


/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220258
  Function Name	: parse_parameters
  Returns Type	: int
  ----Parameter List
  1. struct glb *g,
  2.  int argc,
  3.  char **argv ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int parse_parameters(struct glb *g, int argc, char **argv ) {
	int i;

	if (argc == 1) {
		show_help();
		exit(1);
	}

	for (i = 0; i < argc; i++) {
		if (argv[i][0] == '-') {
			/* parameter */
			switch (argv[i][1]) {

				case 'h':
					show_help();
					exit(1);
					break;

				case 'z':
					i++;
					if (i < argc) {
						g->font_size = atoi(argv[i]);
					} else {
						fprintf(stdout,"Insufficient parameters; -z <font size pts>\n");
						exit(1);
					}
					break;

				case 'p':
					/*
					 * com port can be multiple things in linux
					 * such as /dev/ttySx or /dev/ttyUSBxx
					 */
					i++;
					if (i < argc) {
						g->device = argv[i];
					} else {
						fprintf(stdout,"Insufficient parameters; -p <usb TMC port ie, /dev/usbtmc2>\n");
						exit(1);
					}
					break;

				case 'o':
					/* 
					 * output file where this program will put the text
					 * line containing the information FlexBV will want 
					 *
					 */
					i++;
					if (i < argc) {
						g->output_file = argv[i];
					} else {
						fprintf(stdout,"Insufficient parameters; -o <output file>\n");
						exit(1);
					}
					break;

				case 'd': g->debug = 1; break;

				case 'q': g->quiet = 1; break;

				case 'v':
							 fprintf(stdout,"Build %d\r\n", BUILD_VER);
							 exit(0);
							 break;

				case 't':
							 i++;
							 g->interval = atoi(argv[i]);
							 break;

				case 'c':
							 if (argv[i][2] == 'v') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &g->font_color_pri.r
										 , &g->font_color_pri.g
										 , &g->font_color_pri.b
										 );

							 } else if (argv[i][2] == 'a') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &g->font_color_sec.r
										 , &g->font_color_sec.g
										 , &g->font_color_sec.b
										 );

							 } else if (argv[i][2] == 'b') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &(g->background_color.r)
										 , &(g->background_color.g)
										 , &(g->background_color.b)
										 );

							 }
							 break;

				case 'w':
							 if (argv[i][2] == 'x') {
								 i++;
								 g->wx_forced = atoi(argv[i]);
							 } else if (argv[i][2] == 'y') {
								 i++;
								 g->wy_forced = atoi(argv[i]);
							 }
							 break;

				case 's':
							 i++;
							 g->serial_parameters_string = argv[i];
							 break;

				default: break;
			} // switch
		}
	}

	return 0;
}



/*
 * Default parameters are 2400:8n1, given that the multimeter
 * is shipped like this and cannot be changed then we shouldn't
 * have to worry about needing to make changes, but we'll probably
 * add that for future changes.
 *
 */
void open_port( struct glb *g ) {

	struct serial_params_s *s = &(g->serial_params);
	char *p = g->serial_parameters_string;
	char default_params[] = "115200";
	int r; 

	if (!p) p = default_params;

	if (g->debug) fprintf(stderr,"Attempting to open '%s'\n", s->device);
	s->fd = open( s->device, O_RDWR | O_NOCTTY | O_NDELAY );
	if (s->fd <0) {
		perror( s->device );
	}

	fcntl(s->fd,F_SETFL,0);
	tcgetattr(s->fd,&(s->oldtp)); // save current serial port settings 
	tcgetattr(s->fd,&(s->newtp)); // save current serial port settings in to what will be our new settings
	cfmakeraw(&(s->newtp));

	s->newtp.c_cflag = CS8 |  CLOCAL | CREAD ; 

	if (strncmp(p, "115200", 6) == 0) s->newtp.c_cflag |= B115200; 
	else if (strncmp(p, "57600", 5) == 0) s->newtp.c_cflag |= B57600;
	else if (strncmp(p, "38400", 5) == 0) s->newtp.c_cflag |= B38400;
	else if (strncmp(p, "19200", 5) == 0) s->newtp.c_cflag |= B19200;
	else if (strncmp(p, "9600", 4) == 0) s->newtp.c_cflag |= B9600;
	else {
		fprintf(stdout,"Invalid serial speed\r\n");
		exit(1);
	}

	//  This meter only accepts 8n1, no flow control

	s->newtp.c_iflag &= ~(IXON | IXOFF | IXANY );

	r = tcsetattr(s->fd, TCSANOW, &(s->newtp));
	if (r) {
		fprintf(stderr,"%s:%d: Error setting terminal (%s)\n", FL, strerror(errno));
		exit(1);
	}

	if (g->debug) fprintf(stderr,"Serial port opened, FD[%d]\n", s->fd);
}



int data_read( glb *g, char *b, ssize_t s ) {
	int bp = 0;
	ssize_t bytes_read = 0;

	do {
		char temp_char;
		bytes_read = read(g->serial_params.fd, &temp_char, 1);
		if (bytes_read) {
			b[bp] = temp_char;
			if (b[bp] == '\n') break;
			if (b[bp] != '\r') {
				if (g->debug) fprintf(stderr,"%c", b[bp]);
				bp++;
			}
		}
	} while (bytes_read && bp < s);
	b[bp] = '\0';

	return bp;
}



int data_write( glb *g, const char *d, ssize_t s ) { 
	ssize_t sz;

	if (g->debug) fprintf(stderr,"%s:%d: Sending '%s' [%ld bytes]\n", FL, d, s );
	sz = write(g->serial_params.fd, d, s); 
	if (sz < 0) {
		g->error_flag = true;
		fprintf(stdout,"Error sending serial data: %s\n", strerror(errno));
	}

	return sz;
}


void grab_key(Display* display, Window rootWindow, int keycode, int modifier) {
	XGrabKey(display, keycode, modifier, rootWindow, false, GrabModeAsync, GrabModeAsync);

	if (modifier != AnyModifier) {
		XGrabKey(display, keycode, modifier | Mod2Mask, rootWindow, false, GrabModeAsync, GrabModeAsync);
		XGrabKey(display, keycode, modifier | LockMask, rootWindow, false, GrabModeAsync, GrabModeAsync);
		XGrabKey(display, keycode, modifier | Mod2Mask | LockMask, rootWindow, false, GrabModeAsync, GrabModeAsync);
	}
}


/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220307
  Function Name	: main
  Returns Type	: int
  ----Parameter List
  1. int argc,
  2.  char **argv ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int main ( int argc, char **argv ) {

	SDL_Event event;
	SDL_Surface *surface, *surface_2;
	SDL_Texture *texture, *texture_2;

	char linetmp[SSIZE]; // temporary string for building main line of text

	struct glb g;        // Global structure for passing variables around
	char tfn[4096];
	bool quit = false;
	bool paused = false;

	glbs = &g;

	/*
	 * Initialise the global structure
	 */
	init(&g);

	/*
	 * Parse our command line parameters
	 */
	parse_parameters(&g, argc, argv);
	if (g.device == NULL) {
		fprintf(stdout,"Require valid device (ie, -p /dev/usbtmc2 )\nExiting\n");
		exit(1);
	}

	if (g.debug) fprintf(stdout,"START\n");

	g.comms_mode = CMODE_SERIAL;
	g.serial_params.device = g.device;

	/* 
	 * check paramters
	 *
	 */
	if (g.font_size < 10) g.font_size = 10;
	if (g.font_size > 200) g.font_size = 200;

	if (g.output_file) snprintf(tfn,sizeof(tfn),"%s.tmp",g.output_file);


	open_port( &g );

	Display*    dpy     = XOpenDisplay(0);
	Window      root    = DefaultRootWindow(dpy);
	XEvent      ev;
	Window          grab_window     =  root;


	// Shift key = ShiftMask / 0x01
	// CapLocks = LockMask / 0x02
	// Control = ControlMask / 0x04
	// Alt = Mod1Mask / 0x08
	//
	// Numlock = Mod2Mask / 0x10
	// Windows key = Mod4Mask / 0x40

	grab_key(dpy, grab_window, XKeysymToKeycode(dpy,XK_r), Mod4Mask|Mod1Mask);
	grab_key(dpy, grab_window, XKeysymToKeycode(dpy,XK_v), Mod4Mask|Mod1Mask);
	grab_key(dpy, grab_window, XKeysymToKeycode(dpy,XK_c), Mod4Mask|Mod1Mask);
	grab_key(dpy, grab_window, XKeysymToKeycode(dpy,XK_d), Mod4Mask|Mod1Mask);
	grab_key(dpy, grab_window, XKeysymToKeycode(dpy,XK_f), Mod4Mask|Mod1Mask);
	XSelectInput(dpy, root, KeyPressMask);


	/*
	 * Setup SDL2 and fonts
	 *
	 */

	SDL_Init(SDL_INIT_VIDEO);
	TTF_Init();
	TTF_Font *font = TTF_OpenFont("RobotoMono-Regular.ttf", g.font_size);
	TTF_Font *font_small = TTF_OpenFont("RobotoMono-Regular.ttf", g.font_size/2);

	/*
	 * Get the required window size.
	 *
	 * Parameters passed can override the font self-detect sizing
	 *
	 */
	TTF_SizeText(font, " 00.0000V DCAC ", &g.window_width, &g.window_height);
	g.window_height *= 1.85;

	if (g.wx_forced) g.window_width = g.wx_forced;
	if (g.wy_forced) g.window_height = g.wy_forced;

	SDL_Window *window = SDL_CreateWindow("gdm-8341", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, g.window_width, g.window_height, 0);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
	if (!font) {
		fprintf(stderr,"Error trying to open font :( \r\n");
		exit(1);
	}

	/* Select the color for drawing. It is set to red here. */
	SDL_SetRenderDrawColor(renderer, g.background_color.r, g.background_color.g, g.background_color.b, 255 );

	/* Clear the entire screen to our selected color. */
	SDL_RenderClear(renderer);

	/*
	 *
	 * Parent will terminate us... else we'll become a zombie
	 * and hope that the almighty PID 1 will reap us
	 *
	 */
	while (!quit) {
		char line1[1024];
		char line2[1024];
		char line3[1024];
		char buf[100];
		char range[100];
		char value[100];
		char rs[100];

		int mi = 0;

		if (!paused && !quit) {
			if (XCheckMaskEvent(dpy, KeyPressMask, &ev)) {
				KeySym ks;
				if (g.debug) fprintf(stderr,"Keypress event %X\n", ev.type);
				switch (ev.type) {
					case KeyPress:
						//					ks = XKeycodeToKeysym(dpy,ev.xkey.keycode,0);
						ks = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
						if (g.debug) fprintf(stderr,"Hot key pressed %X => %lx!\n", ev.xkey.keycode, ks);
						switch (ks) {
							case XK_r:
								data_write( &g, mmodes[MMODES_RES].query, strlen(mmodes[MMODES_RES].query) );
								break;
							case XK_v:
								data_write( &g, mmodes[MMODES_VOLT_DC].query, strlen(mmodes[MMODES_VOLT_DC].query) );
								break;
							case XK_c:
								data_write( &g, mmodes[MMODES_CONT].query, strlen(mmodes[MMODES_CONT].query) );
								break;
							case XK_d:
								data_write( &g, mmodes[MMODES_DIOD].query, strlen(mmodes[MMODES_DIOD].query) );
								break;
							case XK_f:
								data_write( &g, mmodes[MMODES_CAP].query, strlen(mmodes[MMODES_CAP].query) );
								break;
							default:
								break;
						} // keycode
						break;

					default:
						break;
				}
			} // check mask
		}

		while (SDL_PollEvent(&event)) {
			switch (event.type)
			{
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_q) {
						data_write( &g, SCPI_LOCAL, strlen(SCPI_LOCAL) );
						quit = true;
					}
					if (event.key.keysym.sym == SDLK_p) {
						paused ^= 1;
						if (paused == true) data_write( &g, SCPI_LOCAL, strlen(SCPI_LOCAL) );
					}
					break;
				case SDL_QUIT:
					quit = true;
					break;
			}
		}

		linetmp[0] = '\0';


		if (!paused && !quit) {
			data_write( &g, SCPI_FUNC, strlen(SCPI_FUNC));
			usleep(2000);
			data_read( &g, buf, sizeof(buf) );
			for (mi = 0; mi < MMODES_MAX; mi++) {
				if (strcmp(buf, mmodes[mi].scpi)==0) {
					if (g.debug) fprintf(stderr,"%s:%d: HIT on '%s' index %d\n", FL, buf, mi);
					data_write( &g, SCPI_VAL, strlen(SCPI_VAL) );
					data_read( &g, buf, sizeof(buf) );
					data_write( &g, SCPI_RANGE, strlen(SCPI_RANGE) );
					data_read( &g, range, sizeof(range) );
					break;
				}
			}
			if (mi == MMODES_MAX) {
				fprintf(stderr,"%s:%d: Unknown mode '%s'\n", FL, buf);
				continue;
			}

			double v = strtod(buf, NULL);

			snprintf(value, sizeof(value), "%f", v);
			snprintf(rs, sizeof(rs), " ");

			switch (mi) {
				case MMODES_VOLT_DC:
					if (strcmp(range,"0.5")==0) { 
						snprintf(value,sizeof(value),"% 07.2f mV DC", v *1000.0);
						snprintf(rs,sizeof(rs),"500mV");
					}
					else if (strcmp(range, "5")==0) { 
						snprintf(value, sizeof(value), "% 07.4f V DC", v);
						snprintf(rs,sizeof(rs),"5V");
					}
					else if (strcmp(range, "50")==0) { 
						snprintf(value, sizeof(value), "% 07.3f V DC", v);
						snprintf(rs,sizeof(rs),"50V");
					}
					else if (strcmp(range, "500")==0) { 
						snprintf(value, sizeof(value), "% 07.2f V DC", v);
						snprintf(rs,sizeof(rs),"500V");
					}
					else if (strcmp(range, "1000")==0) { 
						snprintf(value, sizeof(value), "% 07.1f V DC", v);
						snprintf(rs,sizeof(rs),"1000V");
					}
					break;

				case MMODES_VOLT_AC:
					if (strcmp(range,"0.5")==0) { 
						snprintf(value,sizeof(value),"% 07.2f mV AC", v *1000.0);
						snprintf(rs,sizeof(rs),"500mV");
					}
					else if (strcmp(range, "5")==0) { snprintf(value, sizeof(value), "% 07.4f V AC", v);
						snprintf(rs,sizeof(rs),"5V");
					}
					else if (strcmp(range, "50")==0) { snprintf(value, sizeof(value), "% 07.3f V AC", v);
						snprintf(rs,sizeof(rs),"50V");
					}
					else if (strcmp(range, "500")==0) { snprintf(value, sizeof(value), "% 07.2f V AC", v);
						snprintf(rs,sizeof(rs),"500V");
					}
					else if (strcmp(range, "750")==0) { snprintf(value, sizeof(value), "% 07.1f V AC", v);
						snprintf(rs,sizeof(rs),"750V");
					}
					break;

				case MMODES_VOLT_DCAC:
					if (strcmp(range,"0.5")==0) snprintf(value,sizeof(value),"% 07.2f mV DCAC", v *1000.0);
					else if (strcmp(range, "5")==0) snprintf(value, sizeof(value), "% 07.4f V DCAC", v);
					else if (strcmp(range, "50")==0) snprintf(value, sizeof(value), "% 07.3f V DCAC", v);
					else if (strcmp(range, "500")==0) snprintf(value, sizeof(value), "% 07.2f V DCAC", v);
					else if (strcmp(range, "750")==0) snprintf(value, sizeof(value), "% 07.1f V DCAC", v);
					break;

				case MMODES_CURR_AC:
					if (strcmp(range,"0.0005")==0) snprintf(value,sizeof(value),"%06.2f %sA AC", v, uu);
					else if (strcmp(range, "0.005")==0) snprintf(value, sizeof(value), "%06.4f mA AC", v);
					else if (strcmp(range, "0.05")==0) snprintf(value, sizeof(value), "%06.3f mA AC", v);
					else if (strcmp(range, "0.5")==0) snprintf(value, sizeof(value), "%06.2f mA AC", v);
					else if (strcmp(range, "5")==0) snprintf(value, sizeof(value), "%06.1f A AC", v);
					else if (strcmp(range, "10")==0) snprintf(value, sizeof(value), "%06.3f A AC", v);
					break;

				case MMODES_CURR_DC:
					if (strcmp(range,"0.0005")==0) snprintf(value,sizeof(value),"%06.2f %sA DC", v, uu);
					else if (strcmp(range, "0.005")==0) snprintf(value, sizeof(value), "%06.4f mA DC", v);
					else if (strcmp(range, "0.05")==0) snprintf(value, sizeof(value), "%06.3f mA DC", v);
					else if (strcmp(range, "0.5")==0) snprintf(value, sizeof(value), "%06.2f mA DC", v);
					else if (strcmp(range, "5")==0) snprintf(value, sizeof(value), "%06.1f A DC", v);
					else if (strcmp(range, "10")==0) snprintf(value, sizeof(value), "%06.3f A DC", v);
					break;

				case MMODES_RES:
					if (strcmp(range,"50E+1")==0) { snprintf(value,sizeof(value),"%06.2f %s", v, oo);
						snprintf(rs,sizeof(rs),"500%s",oo); }
					else if (strcmp(range, "50E+2")==0){ snprintf(value, sizeof(value), "%06.4f k%s", v /1000, oo);
						snprintf(rs,sizeof(rs),"5K%s",oo); }
					else if (strcmp(range, "50E+3")==0){ snprintf(value, sizeof(value), "%06.3f k%s", v /1000, oo);
						snprintf(rs,sizeof(rs),"50K%s",oo); }
					else if (strcmp(range, "50E+4")==0){ snprintf(value, sizeof(value), "%06.2f k%s", v /1000, oo);
						snprintf(rs,sizeof(rs),"500K%s",oo); }
					else if (strcmp(range, "50E+5")==0){ snprintf(value, sizeof(value), "%06.4f M%s", v /1000000, oo);
						snprintf(rs,sizeof(rs),"5M%s",oo); }
					else if (strcmp(range, "50E+6")==0){ snprintf(value, sizeof(value), "%06.3f M%s", v /1000000, oo);
						snprintf(rs,sizeof(rs),"50M%s",oo); }
					if (v >= 51000000000000) snprintf(value, sizeof(value), "O.L");
					break;

				case MMODES_CAP:
					if (strcmp(range,"5E-9")==0) { snprintf(value,sizeof(value),"% 6.3f nF", v *1E+9 );
						snprintf(rs,sizeof(rs),"5nF"); }
					else if (strcmp(range, "5E-8")==0){ snprintf(value, sizeof(value), "% 06.2f nF", v *1E+9);
						snprintf(rs,sizeof(rs),"50nF"); }
					else if (strcmp(range, "5E-7")==0){ snprintf(value, sizeof(value), "% 06.1f nF", v *1E+9);
						snprintf(rs,sizeof(rs),"500nF"); }
					else if (strcmp(range, "5E-6")==0){ snprintf(value, sizeof(value), "% 06.3f %sF", v *1E+6, uu);
						snprintf(rs,sizeof(rs),"5%sF",uu); }
					else if (strcmp(range, "5E-5")==0){ snprintf(value, sizeof(value), "% 06.2f %sF", v *1E+6, uu);
						snprintf(rs,sizeof(rs),"50%sF",uu); }
					if (v >= 51000000000000) snprintf(value, sizeof(value), "O.L");
					break;


				case MMODES_CONT:
					{ 
						char v2[100];
						double threshold;
						data_write( &g, SCPI_CONT_THRESHOLD, strlen(SCPI_CONT_THRESHOLD) );
						data_read( &g, v2, sizeof(v2) );
						threshold = strtod(v2, NULL);
						if (v > threshold) {
							if (v > 1000) v = 999.9;
							snprintf(value, sizeof(value), "OPEN [%05.1f%s]", v, oo);
						}
						else {
							snprintf(value, sizeof(value), "SHRT [%05.1f%s]", v, oo);
						}
						snprintf(rs,sizeof(rs),"None");
					}
					break;

				case MMODES_DIOD:
					{ 
						if (v > 9.999) {
							snprintf(value, sizeof(value), "OPEN / OL");
						} else {
							snprintf(value, sizeof(value), "%06.4f V", v);
						}
						snprintf(rs,sizeof(rs),"None");
					}
					break;


			}
			snprintf(line1, sizeof(line1), "%s", value);
			snprintf(line2, sizeof(line2), "%s, %s", mmodes[mi].label, rs);
			if (g.debug) fprintf(stderr,"Value:%f Range: %s\n", v, range);

		} else {
			snprintf(line1, sizeof(line1),"Paused");
			snprintf(line2, sizeof(line2),"Press p");
		}
		/*
		 *
		 * END OF DATA ACQUISITION
		 *
		 */



		{
			/*
			 * Rendering
			 *
			 *
			 */
			int texW = 0;
			int texH = 0;
			int texW2 = 0;
			int texH2 = 0;
			SDL_RenderClear(renderer);
			surface = TTF_RenderUTF8_Blended(font, line1, g.font_color_pri);
			texture = SDL_CreateTextureFromSurface(renderer, surface);
			SDL_QueryTexture(texture, NULL, NULL, &texW, &texH);
			SDL_Rect dstrect = { 0, 0, texW, texH };
			SDL_RenderCopy(renderer, texture, NULL, &dstrect);

			surface_2 = TTF_RenderUTF8_Blended(font_small, line2, g.font_color_sec);
			texture_2 = SDL_CreateTextureFromSurface(renderer, surface_2);
			SDL_QueryTexture(texture_2, NULL, NULL, &texW2, &texH2);
			dstrect = { 0, texH -(texH /5), texW2, texH2 };
			SDL_RenderCopy(renderer, texture_2, NULL, &dstrect);

			SDL_RenderPresent(renderer);

			SDL_DestroyTexture(texture);
			SDL_FreeSurface(surface);
			if (1) {
				SDL_DestroyTexture(texture_2);
				SDL_FreeSurface(surface_2);
			}

			if (g.error_flag) {
				sleep(1);
			} else {
				usleep(g.interval);
			}


		}


		if (g.output_file) {
			/*
			 * Only write the file out if it doesn't
			 * exist. 
			 *
			 */
			if (!fileExists(g.output_file)) {
				FILE *f;
				if (g.debug) fprintf(stderr,"%s:%d: output filename = %s\r\n", FL, g.output_file);
				f = fopen(tfn,"w");
				if (f) {
					fprintf(f,"%s", linetmp);
					if (g.debug) fprintf(stderr,"%s:%d: %s => %s\r\n", FL, linetmp, tfn);
					fclose(f);
					rename(tfn, g.output_file);
				}
			}
		}

	} // while(1)

	if (g.comms_mode == CMODE_USB) {
		close(g.usb_fhandle);
	}

	XCloseDisplay(dpy);

	TTF_CloseFont(font);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_Quit();
	SDL_Quit();

	return 0;

}
