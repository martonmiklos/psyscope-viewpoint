psyscope-viewpoint
==================

This is an extension for the [PsyScope](http://psy.ck.sissa.it) to be able to interact with the [Arrington Research ViewPoint Eyetracker](http://www.arringtonresearch.com/) application.

The following ViewPoint API's calls are exposed to the PsyScope (followed by the the extension command names):

 - Connect (Connect) 	-	Uses one argument: the ViewPoint server's IP address and port (separated by ':' )
 - SendCommand (SendCommand)	-	Sends the first argument as a command string to the ViewPoint
 - GetGazePoint (GazePoint)	-	First argument is unused, pass the proper variable names to the X and Y parameters
 - GetGazeAngleSmoothed2 (GazeAngle)	-	First argument: 0 or 1 (left or right eye), pass the proper variable names to the X and Y parameters 
 - GetFixationSeconds2 (Fixation)	-	First argument: 0 or 1 (left or right eye), pass the proper variable name to the X to retrive the fixation of the selected eye
 - GetTotalVelocity2 (Velocity)	-	First argument: 0 or 1 (left or right eye), pass the proper variable name to the X to retrive the velocity of the selected eye
 - GetPupilSize2 (PupilSize)	-	First argument: 0 or 1 (left or right eye), pass the proper variable name to the X to retrive the pupil size of the selected eye

 - GetHitListLength (ROIHitTotal)	-	First argument: 0 or 1 (left or right eye), pass the proper variable name to the X to retrive the ROI hit count of the selected eye
 - GetHitListItem (ROIInsideList)	-	First argument: 0 or 1 (left or right eye), pass the proper variable name to the X to retrive the ROI hit count for the index specified in the data parameter
 - GetEventListItem (ROIEnterLeaveList)	-	First argument: 0 or 1 (left or right eye), pass the proper variable name to the X to retrive the gaze's ROI border cross count for the index specified in the data field of the selected eye

 - GetStoreTime2 (HighPrecisionTime)	-	First argument: 0 or 1 (left or right eye), pass the proper variable name to the X to retrive last image capture timestamp

The development of this plugin is sponsored by the [Department of General and Applied Linguistics of the University of Debrecen](http://lingua.arts.unideb.hu/index_en.php)

