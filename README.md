psyscope-viewpoint
==================

This is an extension for the [PsyScope](http://psy.ck.sissa.it) to be able to interact with the [Arrington Research ViewPoint Eyetracker](http://www.arringtonresearch.com/) application.

The following ViewPoint API's calls are exposed to the PsyScope (followed by the the extension command names):

 - Connect (Connect) 	-	Uses one argument the data: the ViewPoint server's IP address and port (separated by ':' )
 - SendCommand (SendCommand)	-	Sends the data argument as a command string to the ViewPoint
 - GetGazePoint (GazePoint)	-	Data parameter is unused, pass the proper variable names to the Variable1 and Variable2 parameters to retrive the X and Y coordinates
 - GetGazeAngleSmoothed2 (GazeAngle)	-	Data parameter is unused, pass the proper variable names to the Variable1 and Variable2 parameters to retrive the X and Y coordinates 
 - GetFixationSeconds2 (Fixation)	-	Data parameter is unused, pass the proper variable names to the Variable1 to retrive the fixation of the selected eye
 - GetTotalVelocity2 (Velocity)	-	Data parameter is unused, pass the proper variable names to the Variable1 parameter to retrive the velocity of the selected eye
 - GetPupilSize2 (PupilSize)	-	Data parameter is unused, pass the proper variable names to the Variable1 parameter to retrive the pupil size of the selected eye

 - GetHitListLength (ROIHitTotal)	-	Data parameter is unused, pass the proper variable names to the Variable1 to retrive the ROI hit count of the selected eye
 - GetHitListItem (ROIInsideList)	-	Pass the proper variable names to the Variable1 to retrive the ROI hit count for the index specified in the data parameter
 - GetEventListItem (ROIEnterLeaveList)	-	Pass the proper variable name to the Variable1 parameter to retrive the gaze's ROI border cross count for the index specified in the Data field of the selected eye

 - GetStoreTime2 (HighPrecisionTime)	-	Pass the proper variable name to the Variable1 parameter to retrive last image capture timestamp

The development of this plugin is sponsored by the [Department of General and Applied Linguistics of the University of Debrecen](http://lingua.arts.unideb.hu/index_en.php)

