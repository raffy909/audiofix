#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <log.h>
#include <uevent.h>
#include <tinyalsa/asoundlib.h>

#define LOG_TAG "audiofix"

#define H2W_STATE_FD "/sys/class/switch/h2w/state"

#define UEVENT_MSG_LEN 4096

#define CARD_NUM 0

#define EXT_SPEAKER_SWITCH_CTRL     "Ext_Speaker_Amp_Switch"
#define EXT_HEADPHONE_SWITCH_CTRL   "Ext_Headphone_Amp_Switch"
#define AUDIO_AMP_R_SWITCH			"Audio_Amp_R_Switch"
#define AUDIO_AMP_L_SWITCH			"Audio_Amp_L_Switch" 

#define ON  1
#define OFF 0

typedef struct 
{
    const char *action;
    const char *path;
    const char *subsystem;
    const char *firmware;
    const char *partition_name;
    const char *device_name;
    const char *modalias;
    int partition_num;
    int major;
    int minor;
} Uevent_t;

void setControl(char *name, int value)
{
    struct mixer *mixer1;
    struct mixer_ctl *ctl;

    mixer1 = mixer_open(CARD_NUM);
    if (mixer1 == NULL)
    {
        ALOGE("Failed opening mixer on card");
		return;
    }

    ctl = mixer_get_ctl_by_name(mixer1, name);
    if (ctl == NULL)
    {
        ALOGE("Failed to access control");
        return;
    }

    if (mixer_ctl_set_value(ctl, 0, value) != 0)
    {
        perror("Filed to set value");
    }
    int val = mixer_ctl_get_value(ctl, 0);
    ALOGD("Value for %s is %d \n", name, val);
}

int readH2wState(int fd)
{
    char state[3];
    ssize_t s;

    lseek(fd, 0L, SEEK_SET);
    s = read(fd, state, 3);

    return atoi(state);
}

void UpdateAudioInterface(int h2wStatefd)
{
    int state = readH2wState(h2wStatefd);
    if (state > 0)
    {
        ALOGI("Headphones connected\n");
        setControl(AUDIO_AMP_R_SWITCH, ON);
        setControl(AUDIO_AMP_L_SWITCH, ON);
        setControl(EXT_SPEAKER_SWITCH_CTRL, OFF);
        setControl(EXT_HEADPHONE_SWITCH_CTRL, ON);
    }
    else
    {
        ALOGI("Headphones disconnected\n");
        setControl(AUDIO_AMP_R_SWITCH, ON);
        setControl(AUDIO_AMP_L_SWITCH, ON);
        setControl(EXT_HEADPHONE_SWITCH_CTRL, OFF);
        setControl(EXT_SPEAKER_SWITCH_CTRL, ON);
    }
}

//uevent_listener.cpp#32
void ParseEvent(const char *msg, Uevent_t *uevent)
{
    uevent->partition_num = -1;
    uevent->major = -1;
    uevent->minor = -1;
    uevent->action = "";
    uevent->path = "";
    uevent->subsystem = "";
    uevent->firmware = "";
    uevent->partition_name = "";
    uevent->device_name = "";
    uevent->modalias = "";
    // currently ignoring SEQNUM
    while (*msg)
    {
        if (!strncmp(msg, "ACTION=", 7))
        {
            msg += 7;
            uevent->action = msg;
        }
        else if (!strncmp(msg, "DEVPATH=", 8))
        {
            msg += 8;
            uevent->path = msg;
        }
        else if (!strncmp(msg, "SUBSYSTEM=", 10))
        {
            msg += 10;
            uevent->subsystem = msg;
        }
        else if (!strncmp(msg, "FIRMWARE=", 9))
        {
            msg += 9;
            uevent->firmware = msg;
        }
        else if (!strncmp(msg, "MAJOR=", 6))
        {
            msg += 6;
            uevent->major = atoi(msg);
        }
        else if (!strncmp(msg, "MINOR=", 6))
        {
            msg += 6;
            uevent->minor = atoi(msg);
        }
        else if (!strncmp(msg, "PARTN=", 6))
        {
            msg += 6;
            uevent->partition_num = atoi(msg);
        }
        else if (!strncmp(msg, "PARTNAME=", 9))
        {
            msg += 9;
            uevent->partition_name = msg;
        }
        else if (!strncmp(msg, "DEVNAME=", 8))
        {
            msg += 8;
            uevent->device_name = msg;
        }
        else if (!strncmp(msg, "MODALIAS=", 9))
        {
            msg += 9;
            uevent->modalias = msg;
        }
        // advance to after the next \0
        while (*msg++)
            ;
    }
}

int main()
{
    int ufd;
    int h2wStatefd;
    struct pollfd pfd;
    char msg[UEVENT_MSG_LEN + 2];
    ssize_t s;
    Uevent_t evt;

    ufd = uevent_open_socket(UEVENT_MSG_LEN, true);
    if (ufd == -1)
    {
        ALOGE("Filed opening socket, Exiting");
        exit(1);
    }

    h2wStatefd = open(H2W_STATE_FD, O_RDONLY);
    if (h2wStatefd == -1)
    {
        ALOGE("Filed to open h2w state, Exiting");
        exit(1);
    }

    pfd.fd = ufd;
    pfd.events = POLLIN;

    UpdateAudioInterface(h2wStatefd);
    
    while (1)
    {
        int nr = poll(&pfd, 1, 100);
        if (nr == 0)
            continue;
        if (nr < 0)
            continue;

        if (pfd.revents & POLLIN)
        {
            s = uevent_kernel_multicast_recv(ufd, (void *)msg, UEVENT_MSG_LEN);
            ParseEvent(msg, &evt);
            if (strcmp(evt.action, "change") == 0 && strcmp(evt.path, "/devices/virtual/switch/h2w") == 0)
            {
                UpdateAudioInterface(h2wStatefd);
            }
        }
    }
}
