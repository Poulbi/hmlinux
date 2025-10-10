static int shm_error;
static int (*X_handler)(Display *, XErrorEvent *) = NULL;

static bool have_mitshm(Display *dpy)
{
    // Only use shared memory on local X servers
    return X11_XShmQueryExtension(dpy) ? SDL_X11_HAVE_SHM : false;
}

static int shm_errhandler(Display *d, XErrorEvent *e)
{
    if (e->error_code == BadAccess) {
        shm_error = True;
        return 0;
    }
    return X_handler(d, e);
}


{
    if (have_mitshm(display)) {
        XShmSegmentInfo shminfo;

        shminfo.shmid = shmget(IPC_PRIVATE, (size_t)h * (*pitch), IPC_CREAT | 0777);
        if (shminfo.shmid >= 0) {
            shminfo.shmaddr = (char *)shmat(shminfo.shmid, 0, 0);
            shminfo.readOnly = False;
            if (shminfo.shmaddr != (char *)-1) {
                shm_error = False;
                X_handler = X11_XSetErrorHandler(shm_errhandler);
                X11_XShmAttach(display, shminfo);
                XSync(display, False);
                X11_XSetErrorHandler(X_handler);
                if (shm_error) {
                    shmdt(shminfo.shmaddr);
                }
            } else {
                shm_error = True;
            }
            shmctl(shminfo.shmid, IPC_RMID, NULL);
        } else {
            shm_error = True;
        }
        if (!shm_error){
            ximage = X11_XShmCreateImage(display, data->visual,
                                               vinfo.depth, ZPixmap,
                                               shminfo.shmaddr, shminfo,
                                               w, h);
            if (!ximage) {
                X11_XShmDetach(display, shminfo);
                X11_XSync(display, False);
                shmdt(shminfo.shmaddr);
            } else {
                // Done!
                ximage->byte_order = (SDL_BYTEORDER == SDL_BIG_ENDIAN) ? MSBFirst : LSBFirst;
                *pixels = shminfo.shmaddr;
                return true;
            }
        }
    }
}

X11_XShmPutImage(display, data->xwindow, data->gc, data->ximage, x, y, x, y, w, h, False);

