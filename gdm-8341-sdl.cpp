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
#include <sys/file.h>
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
	char logmode[10];
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


#define READSTATE_NONE		0
#define READSTATE_READING_FUNCTION	1
#define READSTATE_FINISHED_FUNCTION 2
#define READSTATE_READING_VAL 3
#define READSTATE_FINISHED_VAL 4
#define READSTATE_READING_RANGE 5
#define READSTATE_FINISHED_RANGE 6
#define READSTATE_READING_CONTLIMIT 7
#define READSTATE_FINISHED_CONTLIMIT 8
#define READSTATE_FINISHED_ALL 9
#define READSTATE_DONE		10
#define READSTATE_ERROR 999

#define READ_BUF_SIZE 4096

struct mmode_s mmodes[] = { 
	{"VOLT", "Volts DC", "MEAS:VOLT:DC?\r\n", "V DC", "VOLTSDC"}, 
	{"VOLT:AC", "Volts AC", "MEAS:VOLT:AC?\r\n", "V AC", "VOLTSAC"},
	{"VOLT:DCAC", "Volts DC/AC", "MEAS:VOLT:DCAC?\r\n", "V DC/AC", "VOLTSDC"},
	{"CURR", "Current DC", "MEAS:CURR:DC?\r\n", "A DC", "AMPSDC"},
	{"CURR:AC", "Current AC", "MEAS:CURR:AC?\r\n", "A AC", "AMPSAC"},
	{"CURR:DCAC", "Current DC/AC", "MEAS:CURR:DCAC?\r\n", "A DC/AC", "AMPSDC"},
	{"RES", "Resistance", "MEAS:RES?\r\n", oo, "OHMS" },
	{"FREQ", "Frequency", "MEAS:FREQ?\r\n", "Hz", "FREQ" },
	{"PER", "Period", "MEAS:PER?\r\n", "s", "" },
	{"TEMP", "Temperature", "MEAS:TEMP:TCO?\r\n", "C", "TEMP"},
	{"DIOD", "Diode", "MEAS:DIOD?\r\n", "V", "DIODE" },
	{"CONT", "Continuity", "MEAS:CONT?\r\n", oo, "OHMS" },
	{"CAP", "Capacitance", "MEAS:CAP?\r\n", "F", "CAP" }
};

const char SCPI_FUNC[] = "SENS:FUNC1?\r\n";
const char SCPI_VAL1[] = "VAL1?\r\n";
const char SCPI_VAL2[] = "VAL2?\r\n";
const char SCPI_CONT_THRESHOLD[] = "SENS:CONT:THR?\r\n";
const char SCPI_LOCAL[] = "SYST:LOC\r\n";
const char SCPI_RANGE[] = "CONF:RANG?\r\n";

const char SEPARATOR_DP[] = ".";

#ifndef PATH_MAX 
#define PATH_MAX 4096
#endif

struct serial_params_s {
	char device[PATH_MAX];
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
	char device[PATH_MAX];

	int usb_fhandle;

	int comms_mode;
	char *com_address;
	char *serial_parameters_string; // this is the raw from the command line
	struct serial_params_s serial_params; // this is the decoded version

	int mode_index;
	int read_failure;
	int read_state;
	char read_buffer[READ_BUF_SIZE];
	char *bp;
	ssize_t bytes_remaining;

	int cont_threshold;
	double v;
	char value[READ_BUF_SIZE];
	char func[READ_BUF_SIZE];
	char range[READ_BUF_SIZE];

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
	g->read_failure = 0;
	g->read_state = READSTATE_NONE;
	g->mode_index = MMODES_MAX;
	g->cont_threshold = 20.0; // ohms
	g->debug = 0;
	g->quiet = 0;
	g->flags = 0;
	g->error_flag = 0;
	g->output_file = NULL;
	g->interval = 100000; // 100ms / 100,000us interval of sleeping between frames
	g->device[0] = '\0';
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
	fprintf(stdout,"GDM-8341 Multimeter display\r\n"
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
			"\t-o <output file>\r\n"
			"\r\n"
			"\texample: gdm-8341-sdl -p /dev/ttyUSB0 -s 38400\r\n"
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
						snprintf(g->device, PATH_MAX -1, "%s", argv[i]);
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
 * open_port()
 *
 * The GDM-8341 is fixed in the 8n1 parameters but the
 * serial speed can vary between 9600-115200
 *
 * No flow control
 *
 * Default is 115200
 *
 *
 */
int open_port( struct glb *g ) {

	struct serial_params_s *s = &(g->serial_params);
	char *p = g->serial_parameters_string;
	char default_params[] = "115200";
	int r; 

	if (!p) p = default_params;

	if (g->debug) fprintf(stderr,"%s:%d: Attempting to open '%s'\n", FL, s->device);
	s->fd = open( s->device, O_RDWR | O_NOCTTY | O_NDELAY );
	if (s->fd <0) {
		perror( s->device );
		return -1;
	}

	r = flock(s->fd, LOCK_EX | LOCK_NB);
	if (r == -1) {
		close(s->fd); s->fd = -1;
		fprintf(stderr, "%s:%d: Unable to set lock on %s, Error '%s'\n", FL, s->device, strerror(errno) );
		return -1;
	}

	fcntl(s->fd,F_SETFL,0);
	tcgetattr(s->fd,&(s->oldtp)); // save current serial port settings 
	tcgetattr(s->fd,&(s->newtp)); // save current serial port settings in to what will be our new settings
	cfmakeraw(&(s->newtp));

	s->newtp.c_cflag = CS8 |  CLOCAL | CREAD ; 

	s->newtp.c_cc[VTIME] = 10;
	s->newtp.c_cc[VMIN] = 0;

	if (strncmp(p, "115200", 6) == 0) s->newtp.c_cflag |= B115200; 
	else if (strncmp(p, "57600", 5) == 0) s->newtp.c_cflag |= B57600;
	else if (strncmp(p, "38400", 5) == 0) s->newtp.c_cflag |= B38400;
	else if (strncmp(p, "19200", 5) == 0) s->newtp.c_cflag |= B19200;
	else if (strncmp(p, "9600", 4) == 0) s->newtp.c_cflag |= B9600;
	else {
		fprintf(stdout,"Invalid serial speed\r\n");
		close(s->fd); s->fd = -1;
		return -1;
	}

	//  This meter only accepts 8n1, no flow control

	s->newtp.c_iflag &= ~(IXON | IXOFF | IXANY );

	r = tcsetattr(s->fd, TCSANOW, &(s->newtp));
	if (r) {
		fprintf(stderr,"%s:%d: Error setting terminal (%s)\n", FL, strerror(errno));
		close(s->fd); s->fd = -1;
		return -1;
	}

	if (g->debug) fprintf(stderr,"Serial port opened, FD[%d]\n", s->fd);
	return 0;
}

#define PORT_OK 0
#define PORT_CANT_LOCK 10
#define PORT_INVALID 11
#define PORT_CANT_SET 12
#define PORT_NO_SUCCESS -1

int find_port( struct glb *g ) {

	/*
		For this multimeter, we actually *want* the read to time out
		because it means that it's not just spewing out data, ie not
		a SCPI device
		*/
	struct serial_params_s *s = &(g->serial_params);
	g->read_state = READSTATE_NONE;
	g->read_failure = 0;

	for ( int port_number = 0; port_number < 10; port_number++ ) {
		snprintf(s->device, sizeof(s->device) -1, "/dev/ttyUSB%d", port_number);
		if ( g->debug ) fprintf(stderr,"Testing port %s\n", s->device);
		int r = open_port( g );
		if (r == PORT_OK ) {
			int bytes_read = 0;
			fd_set set;
			struct timeval timeout;

			FD_ZERO(&set);
			FD_SET(s->fd, &set);
			timeout.tv_sec = 0;
			timeout.tv_usec = 300000; // 0.3 seconds

			{
				char temp_char = 0;
				int rv;

				bytes_read = 0;
				rv = select(s->fd +1, &set, NULL, NULL, &timeout);
				if (g->debug) fprintf(stderr,"select result = %d\n", rv);
				if (rv == -1) {
					break;
				} else if (rv == 0 ) {
				} else bytes_read = read(s->fd, &temp_char, 1);

				if (g->debug) fprintf(stderr,"%d bytes read after select\n", bytes_read);
				if (bytes_read > 0) {
					/* 
						not our meter!
						*/
					break;
				} else if ((rv == 0) && (bytes_read == 0)) {
					char buf[100];

					if (g->debug) fprintf(stderr,"Testing port with *IDN? query\n");
					size_t bytes_written = write( s->fd, "*IDN?\r\n", strlen("*IDN?\r\n"));
					if (bytes_written > 0) {
						size_t bytes_read = read(s->fd, buf, 99);
						if (bytes_read > 0) {
							buf[bytes_read] = '\0';
							if (g->debug) fprintf(stderr," %ld bytes read, '%s'\n", bytes_read, buf);
							if (strstr(buf,"GDM8341")) {
								fprintf(stderr,"Port %s selected\n", s->device);
								if (g->debug) fprintf(stderr,"Port %s selected\n", s->device);
								return PORT_OK;
							}
						}
					}

				}
			} 

			close(s->fd);
		} // port OK
	} // for each port
	return PORT_NO_SUCCESS;
}



/*
 * data_read()
 *
 * char *b : buffer for data
 * ssize_t s : size of buffer; function returns if size limit is hit
 *
 */
int data_read( glb *g ) {
	int bp = 0;
	ssize_t bytes_read = 0;
	fd_set set;
	struct timeval timeout;

	// Non-blocking new code
	//
	FD_ZERO(&set);
	FD_SET(g->serial_params.fd, &set);
	timeout.tv_sec = 0;
	timeout.tv_usec = 500000; // 0.5 seconds

	g->read_failure++;

	{ 
		int rv;
		bytes_read = 0;

		rv = select(g->serial_params.fd +1, &set, NULL, NULL, &timeout);
		if (g->debug) fprintf(stderr,"select result = %d\n", rv);
		if (rv == -1) {
			return -1;
//			break;
		} else if (rv == 0 ) {
			// Something bad happens here?
		} else {
			bytes_read = read(g->serial_params.fd, g->read_buffer, READ_BUF_SIZE);
			if (bytes_read > 0) {
				char *p = strchr(g->read_buffer, '\n');
				if (p) {
					*p = 0;
					g->read_state++;
					p = strchr(g->read_buffer, '\r');
					if (p) *p = '\0';
				}
				g->read_failure = 0;
			}
		}

	} // New code, non-blocking 


	// Original blocking code
	//
	//
	/*
	do {
		char temp_char;
		bytes_read = read(g->serial_params.fd, &temp_char, 1);
		if (bytes_read) {
			*(g->bp) = temp_char;
			if (*(g->bp) == '\n')  {
				g->read_state++; // switch to next read state
				*(g->bp) = '\0';
				break;
			}

			if (*(g->bp) != '\r') {
				if (g->debug) fprintf(stderr,"%c", *(g->bp));
				(g->bp)++;
				*(g->bp) = '\0';
				g->bytes_remaining--;
			}
		}
	} while (bytes_read && g->bytes_remaining > 0);
	*/
	//
	// End of original blocking code

	return bp;
}



/*
 * data_write()
 *		const char *d : pointer to data to write/send
 *		ssize_t s : number of bytes to send
 *
 */
int data_write( glb *g, const char *d, ssize_t s ) { 
	ssize_t sz;

	if (g->serial_params.fd < 1) {
		fprintf(stderr,"%s:%d: Invalid com port file handle.  Not writing.\n", FL);
		return -1;
	}
	if (g->debug) fprintf(stderr,"%s:%d: Sending '%s' [%ld bytes]\n", FL, d, s );
	sz = write(g->serial_params.fd, d, s); 
	if (sz < 0) {
		g->error_flag = true;
		fprintf(stdout,"Error sending serial data: %s\n", strerror(errno));
	}

	return sz;
}


/*
 * grab_key()
 *
 * Function sets up a global XGrabKey() and additionally registers
 * for num and cap lock keyboard combinations.
 *
 */
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
	if (strlen(g.device) < 1 ) {
		find_port( &g );
	}

	if (g.debug) fprintf(stdout,"START\n");

	g.comms_mode = CMODE_SERIAL;
	snprintf(g.serial_params.device, PATH_MAX , "%s", g.device);

	/* 
	 * check paramters
	 *
	 */
	if (g.font_size < 10) g.font_size = 10;
	if (g.font_size > 200) g.font_size = 200;

	if (g.output_file) snprintf(tfn,sizeof(tfn),"%s.tmp",g.output_file);


	//	find_port( &g );
	//		  open_port( &g );

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
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	if (!font) {
		fprintf(stderr,"Error trying to open font :( \r\n");
		exit(1);
	}
	SDL_RendererInfo info;
	SDL_GetRendererInfo( renderer, &info );

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
	char line1[4096];
	char line2[5000];

	while (!quit) {

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
							case XK_u:
								data_write( &g, mmodes[MMODES_CAP].query, strlen(mmodes[MMODES_CAP].query) );
								break;
							case XK_f:
								data_write( &g, mmodes[MMODES_FREQ].query, strlen(mmodes[MMODES_FREQ].query) );
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


		if (!paused && !quit) {

			if (g.read_failure > 5) {
				g.debug = 1;
				fprintf(stderr,"Excess read failures; trying to reacquire the COM port again.\n");
				if (g.serial_params.fd > 1) {
					close( g.serial_params.fd );
					g.serial_params.fd = -1;
				}
				if (find_port( &g ) != PORT_OK) {
					fprintf(stderr,"Unable to find a port with the multimeter, sleeping for 2 seconds\n");
					sleep(2);
				}
				g.debug = 0;
			}

			if (g.read_state != READSTATE_NONE && g.read_state != READSTATE_DONE) {
				data_read( &g );
			}

			switch (g.read_state) {
				case READSTATE_NONE:
				case READSTATE_DONE:
					data_write( &g, SCPI_FUNC, strlen(SCPI_FUNC));
					g.bp = g.read_buffer; *(g.bp) = '\0'; g.bytes_remaining = READ_BUF_SIZE;
					g.read_state = READSTATE_READING_FUNCTION;
					break;

				case READSTATE_FINISHED_FUNCTION:
					// check the value of the buffer and determine
					// which mode-index (mi) we need for later --- idiot!
					//
					int mi;
					for (mi = 0; mi < MMODES_MAX; mi++) {
						if (strcmp(g.read_buffer, mmodes[mi].scpi)==0) {
							if (g.debug) fprintf(stderr,"%s:%d: HIT on '%s' index %d\n", FL, g.read_buffer, mi);
							break;
						}
					}

					if (mi == MMODES_MAX) {
						fprintf(stderr,"%s:%d: Unknown mode '%s'\n", FL, g.read_buffer);
						continue;
					}

					g.mode_index = mi;

					data_write( &g, SCPI_VAL1, strlen(SCPI_VAL1) );
					g.read_state = READSTATE_READING_VAL;
					g.bp = g.read_buffer; *(g.bp) = '\0'; g.bytes_remaining = READ_BUF_SIZE;
					break;

				case READSTATE_FINISHED_VAL:
					g.v = strtod(g.read_buffer, NULL);
					snprintf(g.value, sizeof(g.value), "%f", g.v);

					data_write( &g, SCPI_RANGE, strlen(SCPI_RANGE) );
					g.read_state = READSTATE_READING_RANGE;
					g.bp = g.read_buffer; *(g.bp) = '\0'; g.bytes_remaining = READ_BUF_SIZE;
					break;

				case READSTATE_FINISHED_RANGE:
					snprintf(g.range, sizeof(g.range), "%s", g.read_buffer);
					if (g.mode_index == MMODES_CONT) { 
						g.bp = g.read_buffer; *(g.bp) = '\0'; g.bytes_remaining = READ_BUF_SIZE;
						data_write( &g, SCPI_CONT_THRESHOLD, strlen(SCPI_CONT_THRESHOLD) );
						g.read_state = READSTATE_READING_CONTLIMIT;
					} else {
						g.read_state = READSTATE_FINISHED_ALL;
					}
					break;

				case READSTATE_FINISHED_CONTLIMIT:
					g.cont_threshold = strtol(g.read_buffer, NULL, 10);
					g.read_state = READSTATE_FINISHED_ALL;
					break;

				case READSTATE_ERROR:
				default:
					snprintf(g.range,sizeof(g.range),"---");
					snprintf(g.value,sizeof(g.value),"---");
					snprintf(g.func,sizeof(g.func),"no data, check port");
					fprintf(stderr,"default readstate reached, error!\n");
					g.read_state = READSTATE_FINISHED_ALL;
					break;
			} // switch readstate

			if (g.read_state == READSTATE_FINISHED_ALL) {
				g.read_state = READSTATE_DONE;


				switch (g.mode_index) {
					case MMODES_VOLT_DC:
						if (strcmp(g.range,"0.5")==0) { 
							snprintf(g.value,sizeof(g.value),"% 07.2f mV DC", g.v *1000.0);
							snprintf(g.range,sizeof(g.range),"500mV");
						}
						else if (strcmp(g.range, "5")==0) { 
							snprintf(g.value, sizeof(g.value), "% 07.4f V DC", g.v);
							snprintf(g.range,sizeof(g.range),"5V");
						}
						else if (strcmp(g.range, "50")==0) { 
							snprintf(g.value, sizeof(g.value), "% 07.3f V DC", g.v);
							snprintf(g.range,sizeof(g.range),"50V");
						}
						else if (strcmp(g.range, "500")==0) { 
							snprintf(g.value, sizeof(g.value), "% 07.2f V DC", g.v);
							snprintf(g.range,sizeof(g.range),"500V");
						}
						else if (strcmp(g.range, "1000")==0) { 
							snprintf(g.value, sizeof(g.value), "% 07.1f V DC", g.v);
							snprintf(g.range,sizeof(g.range),"1000V");
						}
						break;

					case MMODES_VOLT_AC:
						if (strcmp(g.range,"0.5")==0) { 
							snprintf(g.value,sizeof(g.value),"% 07.2f mV AC", g.v *1000.0);
							snprintf(g.range,sizeof(g.range),"500mV");
						}
						else if (strcmp(g.range, "5")==0) { snprintf(g.value, sizeof(g.value), "% 07.4f V AC", g.v);
							snprintf(g.range,sizeof(g.range),"5V");
						}
						else if (strcmp(g.range, "50")==0) { snprintf(g.value, sizeof(g.value), "% 07.3f V AC", g.v);
							snprintf(g.range,sizeof(g.range),"50V");
						}
						else if (strcmp(g.range, "500")==0) { snprintf(g.value, sizeof(g.value), "% 07.2f V AC", g.v);
							snprintf(g.range,sizeof(g.range),"500V");
						}
						else if (strcmp(g.range, "750")==0) { snprintf(g.value, sizeof(g.value), "% 07.1f V AC", g.v);
							snprintf(g.range,sizeof(g.range),"750V");
						}
						break;

					case MMODES_VOLT_DCAC:
						if (strcmp(g.range,"0.5")==0) snprintf(g.value,sizeof(g.value),"% 07.2f mV DCAC", g.v *1000.0);
						else if (strcmp(g.range, "5")==0) snprintf(g.value, sizeof(g.value), "% 07.4f V DCAC", g.v);
						else if (strcmp(g.range, "50")==0) snprintf(g.value, sizeof(g.value), "% 07.3f V DCAC", g.v);
						else if (strcmp(g.range, "500")==0) snprintf(g.value, sizeof(g.value), "% 07.2f V DCAC", g.v);
						else if (strcmp(g.range, "750")==0) snprintf(g.value, sizeof(g.value), "% 07.1f V DCAC", g.v);
						break;

					case MMODES_CURR_AC:
						if (strcmp(g.range,"0.0005")==0) snprintf(g.value,sizeof(g.value),"%06.2f %sA AC", g.v, uu);
						else if (strcmp(g.range, "0.005")==0) snprintf(g.value, sizeof(g.value), "%06.4f mA AC", g.v);
						else if (strcmp(g.range, "0.05")==0) snprintf(g.value, sizeof(g.value), "%06.3f mA AC", g.v);
						else if (strcmp(g.range, "0.5")==0) snprintf(g.value, sizeof(g.value), "%06.2f mA AC", g.v);
						else if (strcmp(g.range, "5")==0) snprintf(g.value, sizeof(g.value), "%06.1f A AC", g.v);
						else if (strcmp(g.range, "10")==0) snprintf(g.value, sizeof(g.value), "%06.3f A AC", g.v);
						break;

					case MMODES_CURR_DC:
						if (strcmp(g.range,"0.0005")==0) snprintf(g.value,sizeof(g.value),"%06.2f %sA DC", g.v, uu);
						else if (strcmp(g.range, "0.005")==0) snprintf(g.value, sizeof(g.value), "%06.4f mA DC", g.v);
						else if (strcmp(g.range, "0.05")==0) snprintf(g.value, sizeof(g.value), "%06.3f mA DC", g.v);
						else if (strcmp(g.range, "0.5")==0) snprintf(g.value, sizeof(g.value), "%06.2f mA DC", g.v);
						else if (strcmp(g.range, "5")==0) snprintf(g.value, sizeof(g.value), "%06.1f A DC", g.v);
						else if (strcmp(g.range, "10")==0) snprintf(g.value, sizeof(g.value), "%06.3f A DC", g.v);
						break;

					case MMODES_RES:
						if (strcmp(g.range,"50E+1")==0) { snprintf(g.value,sizeof(g.value),"%06.2f %s", g.v, oo);
							snprintf(g.range,sizeof(g.range),"500%s",oo); }
						else if (strcmp(g.range, "50E+2")==0){ snprintf(g.value, sizeof(g.value), "%06.4f k%s", g.v /1000, oo);
							snprintf(g.range,sizeof(g.range),"5K%s",oo); }
						else if (strcmp(g.range, "50E+3")==0){ snprintf(g.value, sizeof(g.value), "%06.3f k%s", g.v /1000, oo);
							snprintf(g.range,sizeof(g.range),"50K%s",oo); }
						else if (strcmp(g.range, "50E+4")==0){ snprintf(g.value, sizeof(g.value), "%06.2f k%s", g.v /1000, oo);
							snprintf(g.range,sizeof(g.range),"500K%s",oo); }
						else if (strcmp(g.range, "50E+5")==0){ snprintf(g.value, sizeof(g.value), "%06.4f M%s", g.v /1000000, oo);
							snprintf(g.range,sizeof(g.range),"5M%s",oo); }
						else if (strcmp(g.range, "50E+6")==0){ snprintf(g.value, sizeof(g.value), "%06.3f M%s", g.v /1000000, oo);
							snprintf(g.range,sizeof(g.range),"50M%s",oo); }
						if (g.v >= 51000000000000) snprintf(g.value, sizeof(g.value), "OL");
						break;

					case MMODES_CAP:
						if (strcmp(g.range,"5E-9")==0) { snprintf(g.value,sizeof(g.value),"% 6.3f nF", g.v *1E+9 );
							snprintf(g.range,sizeof(g.range),"5nF"); }
						else if (strcmp(g.range, "5E-8")==0){ snprintf(g.value, sizeof(g.value), "% 06.2f nF", g.v *1E+9);
							snprintf(g.range,sizeof(g.range),"50nF"); }
						else if (strcmp(g.range, "5E-7")==0){ snprintf(g.value, sizeof(g.value), "% 06.1f nF", g.v *1E+9);
							snprintf(g.range,sizeof(g.range),"500nF"); }
						else if (strcmp(g.range, "5E-6")==0){ snprintf(g.value, sizeof(g.value), "% 06.3f %sF", g.v *1E+6, uu);
							snprintf(g.range,sizeof(g.range),"5%sF",uu); }
						else if (strcmp(g.range, "5E-5")==0){ snprintf(g.value, sizeof(g.value), "% 06.2f %sF", g.v *1E+6, uu);
							snprintf(g.range,sizeof(g.range),"50%sF",uu); }
						if (g.v >= 51000000000000) snprintf(g.value, sizeof(g.value), "OL");
						break;


					case MMODES_CONT:
						{ 
							if (g.v > g.cont_threshold) {
								if (g.v > 1000) g.v = 999.9;
								snprintf(g.value, sizeof(g.value), "OPEN [%05.1f%s]", g.v, oo);
							}
							else {
								snprintf(g.value, sizeof(g.value), "SHRT [%05.1f%s]", g.v, oo);
							}
							snprintf(g.range,sizeof(g.range),"Threshold: %d%s", g.cont_threshold, oo);
						}
						break;

					case MMODES_DIOD:
						{ 
							if (g.v > 9.999) {
								snprintf(g.value, sizeof(g.value), "OL / OPEN");
							} else {
								snprintf(g.value, sizeof(g.value), "%06.4f V", g.v);
							}
							snprintf(g.range,sizeof(g.range),"None");
						}
						break;


				}
				snprintf(line1, sizeof(line1), "%s", g.value);
				snprintf(line2, sizeof(line2), "%s, %s", mmodes[g.mode_index].label, g.range);
				if (g.debug) fprintf(stderr,"Value:%f Range: %s\n", g.v, g.range);

			}
		} else if ( paused ) {
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
				f = fopen(tfn,"w");
				if (f) {
					fprintf(f,"%s\t%s", line1, mmodes[g.mode_index].logmode);
					fclose(f);
					chmod(tfn, S_IROTH|S_IWOTH|S_IRUSR|S_IWUSR);
					rename(tfn, g.output_file);
				}
			}
		}

	} // while(1)

	if (g.comms_mode == CMODE_USB) {
		close(g.usb_fhandle);
	}



	close(g.serial_params.fd);
	flock(g.serial_params.fd, LOCK_UN);


	XCloseDisplay(dpy);

	TTF_CloseFont(font);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_Quit();
	SDL_Quit();

	return 0;

}
