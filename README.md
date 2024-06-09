# System-Level-Screen-Capture(C++)
This is an application that captures the system level screen.

1. Make screenshots folder in C Driver.
2. Copy SystemLevelScreenCapture.exe.
3. Run exe file using this command.
      psexec -s -i -h -x -d C:\screenshot\capture.exe
4. Then you can see images in screenshots folder.
5. If you want to kill process using this method.
tasklist /FI "IMAGENAME eq SystemLevelScreenCapture.exe"
taskkill /PID PID /F
