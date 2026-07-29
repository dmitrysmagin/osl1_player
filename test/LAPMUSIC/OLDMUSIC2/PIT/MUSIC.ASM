
        dosseg

        .model small

        .stack 400h

        LOCALS
        JUMPS

saver   MACRO 
        push ax
        push bx
        push cx
        push dx
        push di
        push si
        push ds
        push es
        push bp
        pushf
        ENDM

restore MACRO
        popf
        pop bp
        pop es
        pop ds
        pop si
        pop di
        pop dx
        pop cx
        pop bx
        pop ax
        ENDM

        .code
start:
        mov ax,ds
        mov es,ax
        mov ax,@data
        mov ds,ax
        mov ah,49h
        int 21h         ;deallocate all pos mem
;
        mov bx,01000h   ;64k 
        mov ah,48h
        int 21h 
;
        mov bx,10000h/16
        mov ah,48h
        int 21h
        mov [driverseg],ax
	;add ax,200h/16
	mov [init_driver+2],ax
;
        mov bx,20000h/16
        mov ah,48h
        int 21h
        mov [datseg],ax
;
	mov dx,OFFSET drivername
        mov bx,[driverseg]
        mov cx,65000
	push ds
        call load_file
	pop ds
;
        mov ax,@data
        mov ds,ax
        mov ax,2
        int 10h
        mov bx,OFFSET text
@@1:
        mov dl,[bx]
        inc bx
        or dl,dl
        jz @@out
        mov ah,2
        int 21h
        jmp @@1
@@out:
	call DWORD PTR cs:init_driver
	mov ax,0ah
	int 60h				; internal ints on
res2:
        mov ax,@data
        mov ds,ax
	mov es,ax
	call load_and_start
	call dump_titles

mainloop:
	mov ah,0bh
	int 21h
	or al,al
	jz mainloop
;
        mov ah,7       
	push ds
	int 21h
	pop ds
;
	mov bx,OFFSET checkkey
@@1:
	cmp BYTE PTR [bx],-1
	jz EXIT
	cmp al,[bx]
	jz @@got
	add bx,4
	jmp @@1
@@got:
	mov al,[bx+1]
	mov [whichload],al
	mov al,[bx+2]	
	mov [tunestart],al
	mov di,[tune_handle]
	mov ax,04h
	int 60h			;call DWORD PTR cs:stop_tune
	jmp res2

EXIT:
	cmp al,27
	jnz mainloop

error:
;	mov cl,1
;	mov ax,05h
;	int 60h
;@@l1:
;	mov ax,06h
;	int 60h
;	or al,al
;	jnz @@l1
;
	mov di,[tune_handle]
        mov ax,04h
	int 60h			;call DWORD PTR cs:stop_tune
;        mov di,[tune_handle2]
;	mov ax,04h
;	int 60h
;	
	xor ax,ax
	int 60h			;call DWORD PTR cs:kill_driver

;
        mov ax,2
        int 10h
        mov ax,ds
        mov es,ax
        mov ah,49h
        int 21h
        mov ah,4ch
        int 21h


load_and_start:
	mov bl,[whichload]
	cmp bl,[oldfile]
	jz res3
	mov [oldfile],bl
	xor bh,bh
	shl bx,1
	mov dx,[fnames+bx]
        mov bx,[datseg]
        mov cx,65000
	push ds
        call load_file
	pop ds
;
        mov dx,[datseg]
        xor si,si
	mov ax,2
	int 60h			; afterload
res3:
	xor cx,cx
	mov cl,[tunestart]
	mov ax,03h
	int 60h			;call DWORD PTR cs:start_tune
	mov [tune_handle],di
	;mov cx,testval2
	;mov ax,03h
	;int 60h
	;mov [tune_handle2],di
	ret

;IN:
;dx = filename
;bx = load addr (in paras)
;cx = length

load_file:	
        mov ax,@data
        mov ds,ax
        push bx
        push cx
        call openin
        mov [filehand],ax
;
        mov bx,[filehand]
        pop cx
        pop ds
        xor dx,dx
        push ds
        push bx
        call read
        pop bx
        pop ds
        call closein
        ret

openin:
        mov al,0
        mov ah,3dh
        int 21h
        jc error
        ret

read:   mov ah,3fh
        int 21h
        jc error
        ret

closein:
        mov ah,3eh
        int 21h
        ret


dump_data:
     	saver
	mov ax,0b800h
	mov es,ax
	xor di,di
	mov ax,cs:[driverseg]
	add ax,200h/16
	mov ds,ax
	mov cx,8
@@1:
	push cx si di
	mov cx,21
@@2:
	mov al,[si]
	inc si
	call phex
	mov ax,0720h
	stosw
	loop @@2
	pop di si cx
	add si,21
	add di,160
	loop @@1
;
	restore
	ret

dump_data2:
     	saver
	mov ax,0b800h
	mov es,ax
	mov di,160*10
	mov ax,cs:[driverseg]
	add ax,200h/16
	mov ds,ax
	mov cx,8
@@1:
	push cx si di
	mov cx,80/3
@@2:
	mov al,[si]
	inc si
	call phex
	mov ax,0720h
	stosw
	loop @@2
	pop di si cx
	add si,32
	add di,160
	loop @@1
;
	restore
	ret




phex:
	push ax
	shr al,1
	shr al,1
	shr al,1
	shr al,1
	call phex2
	pop ax
phex2:
	push bx
	mov bl,al
	and bx,0fh
	mov al,cs:[OFFSET cs:hexvals+bx]
	mov ah,7
	stosw
	pop bx
	ret

	

dump_titles:
	test BYTE PTR [flag],-1
	jz @@ok
	ret
@@ok:
	mov BYTE PTR [flag],-1
;
	saver
	mov ax,@data
	mov ds,ax
	mov es,[datseg]
	mov si,158h
	mov di,OFFSET values
	mov cx,10
@@1:
	push cx
	mov cx,6
@@2:
	push cx
	mov dl,[di]
	mov ah,2
	int 21h
	mov ah,2
	mov dl,20h
	int 21h
;
	mov cx,10
@@3:
	mov dl,es:[si]
	mov ah,2
	cmp dl,20h
	jae @@4
	mov dl,20h
@@4:
	cmp dl,80h
	jb @@5
	mov dl,20h
@@5:
	int 21h
	inc si
	loop @@3
	mov dl,20h
	mov ah,2h
	int 21h
	pop cx
	inc di
	add si,16-10
	loop @@2
	mov dl,0ah
	mov ah,2
	int 21h
	mov dl,0dh
	mov ah,2
	int 21h
	pop cx
	loop @@1
	restore
	ret

               .DATA

filehand dw 0

datseg  dw 0

fnames	dw fname1 ;,fname2

fname1	db 'roland.rld',0
;fname2	db 'roland.rld',0

text db 'Roland music driver v. 0.60 , (c) 1991 Imagitec Design Ltd.',0ah,0dh
 db 'Music Written by Matt something.',0ah,0dh
 db 'This Remix by Barry Leitch.',0ah,0dh
  db 'Driver written by Leslie Long.',0ah,0dh,0dh,0ah

 db 0


 ;db 0dh,0ah,'1. Track #1.'
 ;db 0dh,0ah,'2. Track #2.'
 ;db 0dh,0ah,'3. Sfx #1.'
 ;db 0dh,0ah,'4. Sfx #2.'
 ;db 0dh,0ah,'5. Sfx #3.'
 ;db 0dh,0ah,'6. Sfx #4.'
 ;db 0dh,0ah,'7. Sfx #5.'
 ;db 0dh,0ah,'8. Sfx #6.'
 ;db 0dh,0ah,'9. Sfx #7.'
 ;db 0dh,0ah,'a. Sfx #8.'
 ;db 0dh,0ah,'b. Sfx #9.'
 ;db 0dh,0ah
 ;db 0dh,0ah,'ETC.'
 ;
;;
 ;
 ;db 0dh,0ah
 ;db 0dh,0ah
 ;db 'Press any other key to exit...',0ah,0dh,00

values		db '123456789abcdefghijklmnopqrstuvwxyz'
		db          'ABCDEFGHIJKLMNOPQRSTUVWXYZ'

checkkey	db '1',0,0,0
;		db '1',0,0,0
;		db '2',0,1,0
;		db '3',0,2,0
;		db '4',0,3,0
;		db '5',0,4,0
;		db '6',0,5,0
;		db '7',0,6,0
;		db '8',0,7,0
;		db '9',0,8,0
;		db 'a',0,9,0
;
;		db 'b',0,10,0
;		db 'c',0,11,0
;		db 'd',0,12,0
;		db 'e',0,13,0
;		db 'f',0,14,0
;		db 'g',0,15,0
;		db 'h',0,16,0
;		db 'i',0,17,0
;		db 'j',0,18,0
;		db 'k',0,19,0
;
;		db 'l',0,20,0
;		db 'm',0,21,0
;		db 'n',0,22,0
;		db 'o',0,23,0
;		db 'p',0,24,0
;		db 'q',0,25,0
;		db 'r',0,26,0
;		db 's',0,27,0
;		db 't',0,28,0
;		db 'u',0,29,0
;
;		db 'v',0,30,0
;		db 'w',0,31,0
;		db 'x',0,32,0
;		db 'y',0,33,0
;		db 'z',0,34,0
;		db 'A',0,35,0
;		db 'B',0,36,0
;		db 'C',0,37,0
;		db 'D',0,38,0
;		db 'E',0,39,0
;
;		db 'F',0,40,0
;		db 'G',0,41,0
;		db 'H',0,42,0
;		db 'I',0,43,0
;		db 'J',0,44,0
;		db 'K',0,45,0
;		db 'L',0,46,0
;		db 'M',0,47,0
;		db 'N',0,48,0
;		db 'O',0,49,0
;		db 'P',0,50,0
;
;		db 'Q',0,51,0
;		db 'R',0,52,0
;		db 'S',0,53,0
;		db 'T',0,54,0
;		db 'U',0,55,0
;		db 'V',0,56,0
;		db 'W',0,57,0
;		db 'X',0,58,0
;		db 'Y',0,59,0
;		db 'Z',0,60,0
;
		db -1

drivertype	dw 0
whichload	db 0,0
tunestart	db 0,0
oldfile		db -1
driverseg	dw 0


init_driver	dw 0,0

;drivername	db 'adsamp.exe',0
;drivername	db 'adlib.exe',0

drivername	db 'mt32.bin',0

tune_handle	dw 0
tune_handle2	dw 0

hexvals		db '0123456789abcdef'
flag		db 0

        end

