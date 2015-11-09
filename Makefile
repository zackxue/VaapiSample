# NO CONDITION:    Copy va surface to texture in one process
# -DPUT_XWINDOW:   Put va surface to XWindow in one process
# -DFORK_ PROCESS: Fork a child process  to Copy/Put va surface to texture/XWindow
# -DCROSS_PROCESS: Send a message to a process. Let the process to Put va surface to XWindow
#
# P.S. If -DPUT_XWINDOW + -DFORK_PROCESS, please try several times.


CONDITION=-DFORK_PROCESS
export CONDITION

ALL:
	$(MAKE) -C PutSurfaceHost
	$(MAKE) -C vaclient

clean:
	$(MAKE) -C PutSurfaceHost clean
	$(MAKE) -C vaclient clean

