/*
 *  ViewPoint.c
 *  PsyScopeX
 *
 *  Created by mm on 23th of February 2014.
 *
 *
 */
 
 /*
  * ViewPoint Eyetracker Extension for PsyScope
  * An extension to interact with the Arrington Research ViewPoint eyetracker application:
  * http://www.arringtonresearch.com
  * The development of this extension is sponsored by the University of Debrecen
  */
  
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <dlfcn.h>


#include "PSYXS.h"
#include "ExtensionUtils.h"
#include "AccessManager.h"
#include "ViewPoint.h"
#include "TimeUtil.h"

#define DBG_L0 0x1
#define DBG_L1 0x2
#define DBG_L2 0x4
#define DEBUG_SWITCH (DBG_L0|DBG_L1)
#include "DebugUtil.h"

/* ViewPoint video-msg titles */ 
#define ViewPoint_ERROR	"ViewPoint Extension error"
#define ViewPoint_INFO		"ViewPoint Extension info"

#define ViewPoint_DEFAULT_PORT 5000

extern FILE *LogFP;

static int s_ViewPointConnected = 0;

enum {
	ViewPoint_ERR_NO,
    ViewPoint_ERR_INIT
} eViewPointErr;


/**-----------------------------------------------------------------
 /						Status Items
 /--------------------------------------------------------------------*/
typedef enum {
	VPX_STATUS_HEAD                     = 0,
	VPX_STATUS_ViewPointIsRunning       = 1, // returns bool, if true, it may be running on remote machine
	VPX_STATUS_VideoIsFrozen            = 2, // returns bool
	VPX_STATUS_DataFileIsOpen           = 3, // returns bool
	VPX_STATUS_DataFileIsPaused         = 4, // returns bool
	VPX_STATUS_AutoThresholdInProgress  = 5, // returns bool
	VPX_STATUS_CalibrationInProgress    = 6, // returns bool
	VPX_STATUS_StimulusImageShape       = 7, // returns 'I'=isotropic stretch, 'C'=centered, 'F'=fit to window, 'A'=actual
	VPX_STATUS_BinocularModeActive      = 8, // returns bool
	VPX_STATUS_SceneVideoActive         = 9, // returns bool
	VPX_STATUS_DistributorAttached      =10, // returns 0=None, 1=ViewPoint, 2=RemoteLink, what is connected to this local DLL
	VPX_STATUS_CalibrationPoints        =11, // returns the number of calibrtion points: 6,9,12,...,72
	VPX_STATUS_TTL_InValues             =12, // TENTATIVE :: bit code for ttl hardware input channels
	VPX_STATUS_TTL_OutValues            =13, // TENTATIVE :: bit code for ttl hardware output channels
	VPX_STATUS_TAIL
} VPX_StatusItem; // Use with: VPX_GetStatus, eg. after VPX_STATUS_CHANGE notification

typedef struct {
    void **fp;
    char *fname;
} tDyLibFunction;


/**-----------------------------------------------------------------
 /						VIEWPOINT PROGRAM CONSTANTS
 /--------------------------------------------------------------------*/

#define tenK 10000
#define MAX_ROI_BOXES	100
#define MAX_ROI_DISPLAY  10 // shows in real-time display, also max for isoeccentric
#define ROI_NOT_HIT   -9999 // modified 2.8.3.27 for roiEvents (+/-), was: -1
#define ROI_NO_EVENT  -9999 // may be changed to zero.

#define EYE_A 0
#define EYE_B 1
#define VPX_EyeType int
#define SCENE_A		2
#define SCENE_B		3

/**-----------------------------------------------------------------
 /						Data Quality Codes
 /--------------------------------------------------------------------*/
#define VPX_DataQuality					int
#define VPX_QUALITY_PupilScanFailed		5	// pupil scan threshold failed.
#define VPX_QUALITY_PupilFitFailed		4	// pupil could not be fit with an ellipse.
#define VPX_QUALITY_PupilCriteriaFailed	3	// pupil was bad because it exceeded criteria limits.
#define VPX_QUALITY_PupilFallBack		2	// wanted glint, but it was bad, using the good pupil.
#define VPX_QUALITY_PupilOnlyIsGood		1	// wanted only the pupil and got a good one.
#define VPX_QUALITY_GlintIsGood			0	// glint and pupil are good.

#define int_ static int

// typedefs from the VPX API

typedef float VPX_RealType ;
// changing this to double will have broad effects, including: sscanf(s,"%g",&df);

typedef struct {
	VPX_RealType  x ;
	VPX_RealType  y ;
} VPX_RealPoint ;

typedef struct {
	VPX_RealType  left ;
	VPX_RealType  top ;
	VPX_RealType  right ;
	VPX_RealType  bottom ;
} VPX_RealRect ;

typedef struct {
	VPX_RealType  x ;		// horizontal-axis
	VPX_RealType  y ;		// vertical-axis
	VPX_RealType  z ;		// depth-axis
	VPX_RealType  roll ;	// about the z-axis (vertical-axis)
	VPX_RealType  pitch ;	// about the x-axis (horizontal-axis)
	VPX_RealType  yaw ;		// about the y-axis (depth-axis)
} VPX_PositionAngle ;


// function definitions for VPX api
static int32_t (*VPX_ConnectToViewPoint)( char* ipAddress, int32_t port );
static int32_t  (*VPX_GetStatus)( VPX_StatusItem statusRequest );
static int32_t (*VPX_DisconnectFromViewPoint)();
static int32_t (*VPX_so_init)();
static int32_t  (*VPX_VersionMismatch)( double version );
static int (*VPX_SendCommand)( char * szFormat, ...);

/* Non sendcommand features:
GetGazePoint (GazePoint)
GetGazeAngleSmoothed2 (GazeAngle)
GetFixationSeconds2 (Fixation)
GetTotalVelocity2 (Velocity)
GetPupilSize2 (PupilSize)
GetHitListLength (ROIHitTotal)
GetHitListItem (ROIInsideList)
GetEventListItem (ROIEnterLeaveList)
GetStoreTime2 (HighPrecisionTime)
 */
int_ (*VPX_GetGazePoint) ( VPX_RealPoint *gp );
int_ (*VPX_GetGazeAngleSmoothed2)( VPX_EyeType eye, VPX_RealPoint *gp );
int_  (*VPX_GetFixationSeconds2) ( VPX_EyeType eye, double *fs );
int_  (*VPX_GetTotalVelocity2) ( VPX_EyeType eye, double *gp );
int_  (*VPX_GetPupilSize2) ( VPX_EyeType eye, VPX_RealPoint *ps );
int_  (*VPX_ROI_GetHitListLength) ( VPX_EyeType eyn );
int_  (*VPX_ROI_GetHitListItem) ( VPX_EyeType eyn, int NthHit );
int_  (*VPX_ROI_GetEventListItem) ( VPX_EyeType eyn, int NthEvent );
int_  (*VPX_GetStoreTime2) ( VPX_EyeType eyn, double *tm);

// VPX SDK version
#define VPX_SDK_VERSION		285.000


static tDyLibFunction s_VPXFunctiontable[] = {
    { (void **)&VPX_ConnectToViewPoint, "VPX_ConnectToViewPoint" },
    { (void **)&VPX_GetStatus, "VPX_GetStatus" },
    { (void **)&VPX_DisconnectFromViewPoint, "VPX_DisconnectFromViewPoint" },
    { (void **)&VPX_so_init, "VPX_so_init" },
    { (void **)&VPX_VersionMismatch, "VPX_VersionMismatch" },
    { (void **)&VPX_SendCommand, "VPX_SendCommand" },
    { (void **)&VPX_GetGazePoint, "VPX_GetGazePoint" },
    { (void **)&VPX_GetGazeAngleSmoothed2, "VPX_GetGazeAngleSmoothed2" },
    { (void **)&VPX_GetFixationSeconds2, "VPX_GetFixationSeconds2" },
    { (void **)&VPX_GetTotalVelocity2, "VPX_GetTotalVelocity2" },
    { (void **)&VPX_GetPupilSize2, "VPX_GetPupilSize2" },
    { (void **)&VPX_ROI_GetHitListLength, "VPX_ROI_GetHitListLength" },
    { (void **)&VPX_ROI_GetHitListItem, "VPX_ROI_GetHitListItem" },
    { (void **)&VPX_ROI_GetEventListItem, "VPX_ROI_GetEventListItem" },
    { (void **)&VPX_GetStoreTime2, "VPX_GetStoreTime2" },
    { (void **)&VPX_DisconnectFromViewPoint, "VPX_DisconnectFromViewPoint" },
    { NULL, NULL }
};

static void *s_corelib = NULL;


void VPX_SDK_Close() {
    if (s_corelib == NULL)
        return;
    dlclose(s_corelib);
    s_corelib = NULL;
}


int VPX_SDK_Open() {
    tDyLibFunction *pFi; // pointer to loop through the function map
    char dir[1024];
    int dirlen;
    int rc = -1;
    
    dirlen = sizeof(dir);
    GetExecutableDir(dir, (size_t *)&dirlen);
    snprintf(dir + dirlen, sizeof(dir) - dirlen, "/../Resources/libvpx_interapp 22.17.35.dylib");
    s_corelib = dlopen(dir, RTLD_NOW);
    if (s_corelib == NULL)
        goto quit;
    
    pFi = s_VPXFunctiontable;
    while (pFi->fp != NULL) { // loop through the function table and map the functions with dlsym
        *pFi->fp = dlsym(s_corelib, pFi->fname);
        if (*pFi->fp == NULL)
            goto quit;
        pFi++;
    }
    *(dir + dirlen) = 0;
    rc = 0;
    
quit:
    if (rc != 0) {
        fprintf(stderr, "Error loading ViewPoint libraries: %s", dlerror());
        VPX_SDK_Close();
    }
    return rc;
}


//---------------------- ViewPoint stuff -- ON --

#define GetTimeStamp()          (GetRelLocalTimeRef(MS, &s_TimeZero))
#define GetDynViewPointSpec(i)     ((tViewPointSpec*)GetStructFromList(s_DynViewPointSpecList,i))
#define GetViewPointSpec(i)        ((tViewPointSpec*)GetStructFromList(s_ViewPointSpecList,i))
#define GetViewPointLabel(i)       ((char *)GetStructFromList(s_ViewPointLabelList,i))
#define GetViewPointString(i)      ((char *)GetStructFromList(s_ViewPointStringList,i))

typedef struct {
    short idVarCmdLabel;
	short idCmdLabel;
    short idVarCmdString;
    short idCmdString;
    char *data;
    pAccessManager pCmdAccess;
} tViewPointSpec;

static ECSList s_DynViewPointSpecList = NULL;
static ECSList s_ViewPointSpecList = NULL;
static ECSList s_ViewPointLabelList = NULL;
static ECSList s_ViewPointStringList = NULL;

#define AddToDynViewPointSpecList(item) AddToECSList(s_DynViewPointSpecList, (item), NO_DUP)
#define IsInDynViewPointSpecList(item)  IsInList(s_DynViewPointSpecList, item)
#define AddToViewPointSpecList(item) AddToECSList(s_ViewPointSpecList, (item), NO_DUP)
#define IsInViewPointSpecList(item)  IsInList(s_ViewPointSpecList, item)


static void _initViewPointSpec(tViewPointSpec *p) {
    memset(p, 0, sizeof(tViewPointSpec));
    p->idVarCmdLabel = -1;
    p->idCmdLabel = -1;
    p->idVarCmdString = -1;
    p->idCmdString = -1;
    p->data = NULL;
}

static void _clearViewPointSpec(tViewPointSpec *p) {
    if (p->data != NULL)
        free(p->data);
        
    if (p->pCmdAccess)
        AccessManager_Delete(p->pCmdAccess);
}

static long _cmpViewPointSpec_byCmdLabelCmdString(tViewPointSpec *a, tViewPointSpec *b) {
    short v = a->idCmdLabel - b->idCmdLabel;
    //Note: if idCmdString < 0 for a or b or both, then we assume they are the same command 
    return  (v == 0 && a->idCmdString >= 0 &&  b->idCmdString >= 0 ? a->idCmdString - b->idCmdString : v);
}

static long _cmpViewPointSpec(tViewPointSpec *a, tViewPointSpec *b) {
    return a->idCmdLabel - b->idCmdLabel;
}

//---------------------- INTERFACE ON

/* ACTION INTERFACE */

#define ViewPoint_ACT_STR	"ViewPoint"
#define ViewPoint_ACT_CODE	'EYET'
#define VAR             '$'

/*GetGazePoint (GazePoint)
 GetGazeAngleSmoothed2 (GazeAngle)
 GetFixationSeconds2 (Fixation)
 GetTotalVelocity2 (Velocity)
 GetPupilSize2 (PupilSize)
 GetHitListLength (ROIHitTotal)
 GetHitListItem (ROIInsideList)
 GetEventListItem (ROIEnterLeaveList)
 GetStoreTime2 (HighPrecisionTime)*/

// an enum for the action codes
enum {
    ACT_CONNECT,          // for commands connecting to the ViewPoint
    ACT_SEND_COMMAND,     // for commands sending CLI messages to the ViewPoint
    ACT_GET_GAZEPOINT,
    ACT_GET_GAZEANGLE_SMOOTHED,
    ACT_GET_FIXATION_SECONDS,
    ACT_GET_TOTAL_VELOCITY,
    ACT_GET_PUPIL_SIZE,
    ACT_GET_HIT_LIST_LENGHT,
    ACT_GET_HIT_LIST_ITEM,
    ACT_GET_EVENT_LIST_ITEM,
    ACT_GET_STORE_TIME,
    ACT_DISCONNECT,       // for commands closing the ViewPoint connection
};

typedef struct {
	int type;
    int dynamic;
    int idCmd;
    int idVar;
    int sigNum;
    char sigScope;
} tSysCmdAction, *pSysCmdAction;

typedef struct {
	int commandCode;        // this member will contain the requested action code
	char *data;             // this member will hold the command arguments in a null terminated string representation
    int eyeNumber;          // this member will hold the specified eye number (0 for left 1 for right(
    int idX;              // this member will be the id of the X coordinate variable
    int idY;             // this member will be the id of the Y coordinate variable
} tViewPointAction, *pViewPointAction;



// this tag value array contains the script command names and their respective action code
static tTagValuePair s_ViewPointActType[] = {
	{ "Connect",       ACT_CONNECT  },
    { "SendCommand",       ACT_SEND_COMMAND      },
    { "GazePoint", ACT_GET_GAZEPOINT},
    { "GazeAngle", ACT_GET_GAZEANGLE_SMOOTHED},
    { "Fixation", ACT_GET_FIXATION_SECONDS},
    { "Velocity", ACT_GET_TOTAL_VELOCITY},
    { "PupilSize", ACT_GET_PUPIL_SIZE},
    { "ROIHitTotal", ACT_GET_HIT_LIST_LENGHT},
    { "ROIInsideList", ACT_GET_HIT_LIST_ITEM},
    { "ROIEnterLeaveList", ACT_GET_EVENT_LIST_ITEM},
    { "HighPrecisionTime", ACT_GET_STORE_TIME},
    { "Disconnect",    ACT_DISCONNECT   },
	{ _TEND,	_VEND  }
};

static void ViewPoint_ActGetMsgCode(short ignore, GetMsgCodeStruct *params) {
	
	if (!strcasecmp(params->string, ViewPoint_ACT_STR)) {
		params->msgCode = ViewPoint_ACT_CODE;
		return;
	}
	params->msgCode = 0;
}

// passed parameters: Command type, Data, Eye number, X, Y

static void ViewPoint_ActGetProcParams(short ignore, GetPSYXActionParamParams *params) {
    char *prmStrCmd = NULL, *prmStrData = NULL;
    char *dataStr = NULL, *eyeStr = NULL, *xStr = NULL, *yStr = NULL;
	int  err = 1, commandCode = *params->paramc;
	pViewPointAction pViewPointAct;
	
 	assert(params->proc == ViewPoint_ACT_CODE);
	
	if (*params->paramc > 5) {
		sprintf(err_msg, "[Trial %d, Event '%s']\nBad data parameters",
				params->trial, DataGetEventName(params->trial, params->event));
		goto quit;
	}
	
	prmStrCmd = GetParamString(params->params[0]);
	
	if (prmStrCmd == NULL) {
		sprintf(err_msg, "[Trial %d, Event '%s']\nNULL action command param ",
				params->trial, DataGetEventName(params->trial, params->event));
		goto quit;
	}
	
    // find the passed command type in the possible list
	if (TagValuePair_GetValueFromTag(s_ViewPointActType, prmStrCmd, &commandCode) < 0)  {
		sprintf(err_msg, "[Trial %d, Event '%s']\n%s unknown action command",
				params->trial, DataGetEventName(params->trial, params->event), prmStrCmd);
		goto quit;
	}
	
    
    prmStrData = GetParamString(params->params[1]);
	pViewPointAct = (pViewPointAction)IMSMalloc(sizeof(tViewPointAction));
	params->return_params = (Ptr *)IMSMalloc(sizeof(Ptr) * 2);
    
	if (pViewPointAct == NULL || params->return_params == NULL) {
		sprintf(err_msg, "[Trial %d, Event '%s']\nFailed to create action",
				params->trial, DataGetEventName(params->trial, params->event));
		goto quit;
	}
	
	pViewPointAct->commandCode = commandCode;
	pViewPointAct->data = NULL;
	params->return_params[0] = (Ptr)pViewPointAct;
    
    // if string was passed to data then allocate memory and copy data was passed
	if (prmStrData != NULL) {
		pViewPointAct->data = IMSMalloc(strlen(prmStrData) + 1);
		strcpy(pViewPointAct->data, prmStrData);
		params->return_params[1] = pViewPointAct->data; // This for auto IMS mem management
    }
    
    if (*params->paramc >= 3) {
        eyeStr = GetParamString(params->params[2]);
        sscanf(eyeStr, "%d", &pViewPointAct->eyeNumber);
    }
	
    if (*params->paramc >= 4) {
        xStr = GetParamString(params->params[3]);
        pViewPointAct->idX = GetVariableByName(xStr);
    }
    
    if (*params->paramc >= 5) {
        yStr = GetParamString(params->params[4]);
        pViewPointAct->idY = GetVariableByName(yStr);
    }
    
	err = 0;
	
quit:
	if (err)
		MsgPrint(ViewPoint_ERROR, cautionIcon, err_msg, ALLOW_CANCEL+CANCEL_DEFAULT, LogFP);
}


#define DYNCMD(cmd) (cmd->idVarCmdLabel >= 0 || cmd->idVarCmdString >= 0)


static void _VPX_ConnectToViewPoint(char *cmd) {
    char *ipAddr = NULL;
    unsigned int port = ViewPoint_DEFAULT_PORT;
    unsigned int ipAddrLength = 0;
    char *doubleDotIndex = NULL;
    int retCode = 0;

    if (s_ViewPointConnected) { // if ViewPoint is connected throw a warning
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _ViewPoint_ActDo_Connect called when the connection was already established!\n"));
    } else {
        doubleDotIndex = strrchr(cmd, ':'); // look for the doubledot which serves as a separator between the address and the port
        if (doubleDotIndex == NULL) {
            ipAddrLength = strlen(cmd); // dobule dot not found use the default
        } else {
            ipAddrLength = (doubleDotIndex - cmd); // doubledot found
        }
        
        ipAddr = calloc('\0', ipAddrLength * sizeof(char) + 1);
        strncpy(ipAddr, cmd, ipAddrLength);
        
        if (doubleDotIndex != NULL)
            sscanf(doubleDotIndex, ":%u", &port);
        
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _ViewPoint_ActDo_Connect(%s, %d)\n", ipAddr, port));
		retCode = VPX_ConnectToViewPoint(ipAddr, port);
        free(ipAddr);
        if (retCode != 0) {
            sprintf(err_msg, "ViewPointMain - VPX_ConnectToViewPoint failed: %d\n", retCode);
        } else {
            unsigned int timeoutCounter = 10;
            while (timeoutCounter) {
                retCode = VPX_GetStatus(VPX_STATUS_DistributorAttached);
                if (retCode > 0) {
                    break;
                }
                usleep(100000);
                timeoutCounter--;
            }
            
            if (retCode <= 0) {
                sprintf(err_msg, "ViewPointMain - VPX_GetStatus timed out\n");
            } else {
                s_ViewPointConnected = true;
            }
        }
    }
}

static void _VPX_SendCommand(char *cmd) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _ViewPoint_ActDo_SendCommand(%s)\n", cmd));
    if (s_ViewPointConnected) {
        retCode = VPX_SendCommand("%s\n", cmd);
        if (retCode != 0)
            sprintf(err_msg, "ViewPointMain - VPX_SendCommand failed: %d", retCode);
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _ViewPoint_ActDo_SendCommand called with no connection!\n"));
    }
}

static void _VPX_GetGazePoint(tViewPointAction *action)
{
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetGazePoint()\n"));
    if (s_ViewPointConnected) {
        VPX_RealPoint position = {0, 0};
        retCode = VPX_GetGazePoint(&position);
        if (retCode != 1) {
            DEBUG_LEVEL(DBG_L1, printf(err_msg, "ViewPointMain - _VPX_GetGazePoint failed: %d", retCode));
        } else {
            printf("gaze: %f, %f\n", position.x, position.y);
            if (action->idX)
                SetVariableByIdx((short)action->idX, (void*)&position.x, FLOAT, -1);
            if (action->idY)
                SetVariableByIdx((short)action->idY, (void*)&position.y, FLOAT, -1);
        }
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetGazePoint called with no connection!\n"));
    }
}

static void _VPX_GetGazeAngleSmoothed2(tViewPointAction *action) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetGazeAngleSmoothed2()\n"));
    if (s_ViewPointConnected) {
        VPX_RealPoint position = {0, 0};
        retCode = VPX_GetGazeAngleSmoothed2(action->eyeNumber, &position);
        if (retCode != 1) {
            sprintf(err_msg, "ViewPointMain - _VPX_GetGazeAngleSmoothed2 failed: %d", retCode);
        } else {
            if (action->idX)
                SetVariableByIdx((short)action->idX, (void*)&position.x, FLOAT, -1);
            if (action->idY)
                SetVariableByIdx((short)action->idY, (void*)&position.y, FLOAT, -1);
        }
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetGazeAngleSmoothed2 called with no connection!\n"));
    }

}

static void _VPX_GetFixationSeconds2(tViewPointAction *action) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetFixationSeconds2()\n"));
    if (s_ViewPointConnected) {
        double fixation = 0.0f;
        retCode = VPX_GetFixationSeconds2(action->eyeNumber, &fixation);
        if (retCode != 1) {
            sprintf(err_msg, "ViewPointMain - _VPX_GetFixationSeconds2 failed: %d", retCode);
        } else {
            if (action->idX) {
                SetVariableByIdx((short)action->idX, (void*)&fixation, DOUBLE,-1);
                DEBUG_LEVEL(DBG_L1, printf(err_msg, "getfixation: %f\n", fixation));
            }
        }
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetFixationSeconds2 called with no connection!\n"));
    }

}

static void _VPX_GetTotalVelocity2(tViewPointAction *action) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetTotalVelocity2()\n"));
    if (s_ViewPointConnected) {
        double velocity = 0.0f;
        retCode = VPX_GetTotalVelocity2(action->eyeNumber, &velocity);
        if (retCode != 1) {
            sprintf(err_msg, "ViewPointMain - _VPX_GetTotalVelocity2 failed: %d", retCode);
        } else {
            if (action->idX) {
                SetVariableByIdx((short)action->idX, (void*)&velocity, DOUBLE, -1);
            }
        }
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetTotalVelocity2 called with no connection!\n"));
    }
}

static void _VPX_GetPupilSize2(tViewPointAction *action) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetPupilSize2()\n"));
    if (s_ViewPointConnected) {
        VPX_RealPoint retValue = {0.0f, 0.0f};
        retCode = VPX_GetPupilSize2(action->eyeNumber, &retValue);
        if (retCode != 1) {
            sprintf(err_msg, "ViewPointMain - _VPX_GetPupilSize2 failed: %d", retCode);
        } else {
            if (action->idX)
                SetVariableByIdx((short)action->idX, (void*)&retValue.x, FLOAT, -1);
            if (action->idY)
                SetVariableByIdx((short)action->idY, (void*)&retValue.y, FLOAT, -1);
        }
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetPupilSize2 called with no connection!\n"));
    }
}

static void _VPX_ROI_GetHitListLength(tViewPointAction *action) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_ROI_GetHitListLength()\n"));
    if (s_ViewPointConnected) {
        retCode = VPX_ROI_GetHitListLength(action->eyeNumber);
        if (action->idX)
            SetVariableByIdx((short)action->idX, (void*)&retCode, INT,-1);
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_ROI_GetHitListLength called with no connection!\n"));
    }
}

static void _VPX_ROI_GetHitListItem(tViewPointAction *action) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_ROI_GetHitListItem()\n"));
    if (s_ViewPointConnected) {
        int hitIndex = 0;
        sscanf(action->data, "%d", &hitIndex);
        retCode = VPX_ROI_GetHitListItem(action->eyeNumber, hitIndex);
        if (action->idX)
            SetVariableByIdx((short)action->idX, (void*)&retCode, INT, -1);
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_ROI_GetHitListItem called with no connection!\n"));
    }

}

static void _VPX_ROI_GetEventListItem(tViewPointAction *action) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_ROI_GetEventListItem()\n"));
    if (s_ViewPointConnected) {
        int hitIndex = 0;
        sscanf(action->data, "%d", &hitIndex);
        retCode = VPX_ROI_GetEventListItem(action->eyeNumber, hitIndex);
        if (action->idX)
            SetVariableByIdx((short)action->idX, (void*)&retCode, INT,-1);
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_ROI_GetEventListItem called with no connection!\n"));
    }
}

static void _VPX_GetStoreTime2(tViewPointAction *action) {
    int retCode = 0;
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetStoreTime2()\n"));
    if (s_ViewPointConnected) {
        double storeTime = 0;
        retCode = VPX_GetStoreTime2(action->eyeNumber, &storeTime);
        if (action->idX)
            SetVariableByIdx((short)action->idX, (void*)&storeTime, DOUBLE,-1);
    } else {
        DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _VPX_GetStoreTime2 called with no connection!\n"));
    }

}

static void _ViewPoint_ActDo_Disconnect() {
    int retCode = 0;
    
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - _ViewPoint_ActDo_Disconnect()\n"));
    if (s_ViewPointConnected) {
        retCode = VPX_DisconnectFromViewPoint();
        if (retCode != 0)
            sprintf(err_msg, "ViewPointMain - VPX_DisconnectFromViewPoint failed: %d", retCode);
        s_ViewPointConnected = 0;
    }
}

static void ViewPoint_ActDo(short ignore, PSYXActionParams *params) {
	pViewPointAction pViewPointAct;
	
    if (params->paramc > 5)
		return;
	
	pViewPointAct = (pViewPointAction)params->params[0];
    
        
    switch (pViewPointAct->commandCode) {
        case ACT_CONNECT:
            _VPX_ConnectToViewPoint(pViewPointAct->data);
            break;
        case ACT_SEND_COMMAND:
            _VPX_SendCommand(pViewPointAct->data);
            break;
        case ACT_GET_GAZEPOINT:
            _VPX_GetGazePoint(pViewPointAct);
            break;
        case ACT_GET_GAZEANGLE_SMOOTHED:
            _VPX_GetGazeAngleSmoothed2(pViewPointAct);
            break;
        case ACT_GET_FIXATION_SECONDS:
            _VPX_GetFixationSeconds2(pViewPointAct);
            break;
        case ACT_GET_TOTAL_VELOCITY:
            _VPX_GetTotalVelocity2(pViewPointAct);
            break;
        case ACT_GET_PUPIL_SIZE:
            _VPX_GetPupilSize2(pViewPointAct);
            break;
        case ACT_GET_HIT_LIST_LENGHT:
            _VPX_ROI_GetHitListLength(pViewPointAct);
            break;
        case ACT_GET_HIT_LIST_ITEM:
            _VPX_ROI_GetHitListItem(pViewPointAct);
            break;
        case ACT_GET_EVENT_LIST_ITEM:
            _VPX_ROI_GetEventListItem(pViewPointAct);
            break;
        case ACT_GET_STORE_TIME:
            _VPX_GetStoreTime2(pViewPointAct);
            break;
        case ACT_DISCONNECT:
            _ViewPoint_ActDo_Disconnect();
            break;
        default:
            MsgPrint(ViewPoint_ERROR, cautionIcon, "Unknown command", ALLOW_CANCEL+CANCEL_DEFAULT, LogFP);
    }
}

static void _closeViewPointStuff() {
    //TODO dll close here
}

/* OCONNECT */
static void ViewPoint_OConnect(short dummy, OConnectParams *params) {
	/*int err;
	
	err = _GetExpAttributes(params->exp_attribs,  &EAD);
	if (err != ViewPoint_ERR_NO)
		MsgPrint(ViewPoint_ERROR, stopIcon, _GetViewPointErrDescription(err), ALLOW_CANCEL+CANCEL_DEFAULT, LogFP);
	
    err = _initViewPointStuff();
    
    if (err != 0) {
		_DisposeExpAttributes(&EAD);
        _closeViewPointStuff();
		MsgPrint(ViewPoint_ERROR, stopIcon, _GetViewPointErrDescription(err), ALLOW_CANCEL+CANCEL_DEFAULT, LogFP);	
	}
	
quit:
	_DisposeExpAttributes(&EAD);
	params->handles_own_dur = FALSE;
	params->default_dur = "SELF_TERMINATE";*/
}

/* ODISCONNECT */
static void ViewPoint_ODisconnect(short dummy, ODisconnectParams *params) {
	_closeViewPointStuff();
}

/* IDEV INTERFACE --- ON --- */

#define GetViewPointMask(i)   ((tViewPointMask *)GetStructFromList(s_ViewPointMaskList,i))

typedef struct {
   int idVar;       // the id of the variable will contain the command label
   int idCmd;       // this maybe resolved later on, during idev-polling to let specifying condition on future commands
   int idCmdLabel;  // the id of the command label
   int matched;
   infstr actions;
} tViewPointMask;

static ECSList s_ViewPointMaskList = NULL;

static IConnectReturn ViewPoint_IConnect(IConnectParams) {	
    return 1;
}

static IDisconnectReturn ViewPoint_IDisconnect(IDisconnectParams)  {
	return 0;
}

static IMakeMaskReturn ViewPoint_IMakeMask(IMakeMaskParams) {
	tViewPointMask mask, *pMask;
    int ret;

    assert(string != NULL);
    
   //_initViewPointMask(&mask);
    
    if (*string == VAR)
        mask.idVar = GetVariableByName(string + 1);
    else 
    if (*string != '\0') {
        tViewPointSpec cs;
        
        assert((mask.idCmdLabel = AddToECSList(s_ViewPointLabelList, string, NO_DUP)) >= 0);
        cs.idCmdLabel = mask.idCmdLabel;
        mask.idCmd = IsInList(s_ViewPointSpecList, &cs); // maybe even negative, then will be resolved at idev polling time (allow specifyng condition on future commands)
    }
    if (mask.idVar <= 0 && mask.idCmdLabel < 0) {
        snprintf(err_msg, ERR_MSG_BUF_SIZE, "The specification is wrong: '%s'\n"
                         "Format must be: \nCommand Label:[%c<VarName>|<CmdLabel>]\n"
                         "VarName should be a valid variable name\n" 
                         "CmdLabel should be a valid command label", string, VAR);
		MsgPrint(ViewPoint_ERROR, stopIcon, err_msg, FORCE_CANCEL, LogFP);
	}
    
    assert((ret = AddToECSList(s_ViewPointMaskList, &mask, NO_DUP)) >=0);
    pMask = GetViewPointMask(ret);
    // reset condition fields
    pMask->matched = 0;
    if (pMask->actions == NULL) 
        pMask->actions = newinfstr(30) ;
    
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - ViewPoint_IMakeMask() New condition '%s': mask = %d idVar = %d, idCmdLabel = %d, idCmd = %d\n",
                    string, ret, pMask->idVar, pMask->idCmdLabel, pMask->idCmd));
    return ret;
}

static IAddMaskActionReturn ViewPoint_IAddMaskAction(IAddMaskActionParams) {
	infstr actions = GetViewPointMask(maskRef)->actions;
	infaddlong(actions, (long)actionRef);
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - ViewPoint_IAddMaskAction() Added action %p to condition mask %ld\n", actionRef, maskRef));
}

static IDelMaskActionReturn ViewPoint_IDelMaskAction(IDelMaskActionParams) {
	infstr actions = GetViewPointMask(maskRef)->actions;
	short i;
	
    DEBUG_LEVEL(DBG_L1, printf("ViewPoint - ViewPoint_IDelMaskAction() Deleting action for condition mask %ld, %p\n", maskRef, actionRef));
    
	for (i = inflonglen(actions); i--; ) {
		if (inflong(actions, i) == (long)actionRef) {
			infrmveseg(actions, i * sizeof(long), sizeof(long));
			break;
		}
    }
}

static IGetDataStringReturn ViewPoint_IGetDataString(IGetDataStringParams) {
	return "";
}

/* IDEV INTERFACE --- OFF --- */

/* FAKE PROC */
static void ViewPoint_OFake(void) {
}

static short ViewPoint_IFake(void) {
    return TRUE;
}



/* THE MESSAGE TABLE */

static CodeFuncPair *ViewPointMessageTable = 0L;

static void MakeViewPointMessageTable(void) {
	
	ViewPointMessageTable = CreateFunctionTable(
        NULL,
        // idev
        IConnect, ViewPoint_IConnect,
        IInit, ViewPoint_IFake,
        ISuspend, ViewPoint_IFake,
        IResume, ViewPoint_IFake,
        IMakeMask, ViewPoint_IMakeMask,
        IAddMaskAction, ViewPoint_IAddMaskAction,
        IDelMaskAction, ViewPoint_IDelMaskAction,
        IPoll, ViewPoint_IFake,
        IFlush, ViewPoint_IFake,
        IClose, ViewPoint_IFake,
        IDisconnect, ViewPoint_IDisconnect,
        IGetDataString, ViewPoint_IGetDataString,
        // odev
        OConnect, ViewPoint_OConnect,
        ODisconnect, ViewPoint_ODisconnect,
        OInit, ViewPoint_OFake,
        OClose, ViewPoint_OFake,
        OTrialStart, ViewPoint_OFake,
        OTrialEnd, ViewPoint_OFake,
        OSuspend, ViewPoint_OFake,
        OResume, ViewPoint_OFake,
        OAlloc, ViewPoint_OFake,
        OFree, ViewPoint_OFake,
        OLoad, ViewPoint_OFake,
        OUnLoad, ViewPoint_OFake,
        OSplitStimRefNum, ViewPoint_OFake,
        OMakeStimRefNum, ViewPoint_OFake,
        ONewStimOldAttribs, ViewPoint_OFake,
        OPlay, ViewPoint_OFake,
        OClear, ViewPoint_OFake,
        // actions
        pGetMsgCode, ViewPoint_ActGetMsgCode,
        pGetProcParams, ViewPoint_ActGetProcParams,
        ViewPoint_ACT_CODE, ViewPoint_ActDo,
        0L);				
}

short ViewPointMain(long msg, short vers, InitializeStruct *params, long ID) {
    int retCode = 0;
	switch(msg) {
		case pInitialize:
            retCode = VPX_SDK_Open();
            if (retCode != 0) {
                sprintf(err_msg, "ViewPointMain - VPX_SDK_Open failed with %d\n", retCode);
                return retCode;
            }
            
            retCode = VPX_so_init();
            if (retCode != 0) {
                sprintf(err_msg, "ViewPointMain - VPX_so_init failed with %d\n", retCode);
                return retCode;
            }
            
            retCode = VPX_VersionMismatch(VPX_SDK_VERSION);
            if (retCode != 0) {
                sprintf(err_msg, "ViewPointMain - VPX_VersionMismatch %d", retCode);
                return retCode;
            }
            
			InitAllTables(params->tables);
			MakeViewPointMessageTable();
			return 1;
        case pGetFuncTable:
			*(long*)params = (long)ViewPointMessageTable;
			return 1;
        case pDeinitialize:
			Free(ViewPointMessageTable);
            return 1;
        default:
            return 1;
	}
}

//---------------------- INTERFACE OFF

