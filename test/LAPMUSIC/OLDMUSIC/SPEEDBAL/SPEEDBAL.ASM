
;		-----------------------------------
;		Note Data from AdLib "Speedball II"
;		-----------------------------------

;			(c) 1991 Sound Images

songs	dw	Tune_1,Tune_2,Tune_3

Tune_1	db	2,5	; tempo,no of channels
	dw	TuneA1,TuneA2,TuneA3,TuneA4,TuneA5

Tune_2	db	3,6
	dw	TuneB1,TuneB2,TuneB3,TuneB4,TuneB5,TuneB6

Tune_3	db	3,3
	dw	TuneC1,TuneC2,TuneC3

TuneC1	dw	c1p1
TuneC2	dw	c2p1
TuneC3	dw	c3p1

;SNARE
c1p1	db	sn1
	db	L4,65
	db	VOL,78h,L2,65,L6,65
	db	L2,65,L6,65
	db	L2,65,L6,65
	db	L2,65,65,65,65,65,L6,65
	db	cut

c2p1	db	patch6
	db	L4,65
	db	VOL,78h,L2,65,L6,65
	db	L2,65,L6,65
	db	L2,65,L6,65
	db	L2,65,65,65,65,65,L6,65
	db	cut

;BRASS
c3p1	db	patch9,tra,256-26+12
	db	L2,50,50,50
	db	VOL,60h,50
	db	L2,VOL,7Fh,55,VOL,60h,55
	db	L2,VOL,7Fh,55,VOL,60h,55
	db	L2,VOL,7Fh,60,VOL,60h,60
	db	L2,VOL,7Fh,60,VOL,60h,60
	db	L2,VOL,7Fh,65,VOL,60h,65
	db	VOL,7Fh,L2,60,65,60,55,VOL,78h,65
	db	VOL,70h,65,VOL,60h,65,VOL,50h,65
	db	cut

TuneB1	dw	b1p1
TuneB2	dw	b2p1
TuneB3	dw	b3p1
TuneB4	dw	b4p1
TuneB5	dw	b5p1
TuneB6	dw	b6p1

;SNARE
b1p1	db	sn1
	db	L4,65
	db	VOL,70h,65,L2,65,65,L4,65
	db	L2,65,65,65,65,65,65,65,p
	db	VOL,7Fh,L4,65
	db	VOL,70h,65,65,p,65
	db	L2,65,65,65,65,L4,65,p
	db	cut

b2p1	db	patch6
	db	L4,65
	db	VOL,70h,65,L2,65,65,L4,65
	db	L2,65,65,65,65,65,65,65,p
	db	VOL,7Fh,L4,65
	db	VOL,70h,65,65,p,65
	db	L2,65,65,65,65,L4,65,p
	db	cut

;BRASS
b3p1	db	patch9,tra,256-23,L2,53,50
	db	L12,46,L8,48,L4,50
	db	L28,44,L4,55,L8,52
	db	cut

;TOP
b4p1	db	L8,p,lead2,tra,256-9
	db	L2,41,46,L4,51,L2,46,51,56,51,56,61
	db	L4,56
	db	patch10,tra,244,L4,f4,p,f4,p,f4,p,f4,f4,p
	db	cut

;middle
b5p1	db	L10,p,lead2,tra,256-9,vol,70h
	db	L2,41,46,L4,51,L2,46,51,56,51,56,61
	db	L2,56
	db	patch10,tra,244,L4,as4,p,as4,p,as4,p,as4,as4,p
	db	cut

;bottom
b6p1	db	L32,p
	db	patch10,tra,244,L4,ds5,p,ds5,p,ds5,p,ds5,ds5,p
	db	cut


;	-----
;	drums
;	-----

TuneA1	dw	tripwah		;Rest48
LoopA1	dw	A1P8,A1P8

	dw	tripwah,Rest48
	dw	rest48
	
	dw	A1P8,A1P8
	dw	A1P8
	dw	A1P11
	dw	tripwahb	;rest48

  	dw	A1P14
	dw	A1P8,A1P8,A1P8,a1p8x

	dw	A1P15
TestA1	dw	A1P16
	dw	A1P8
	dw	A1P8,A1P8,A1P8
	
	dw	A1P8,A1P8,A1P8,A1P8,a1p8x2
	dw	rest30
	dw	0,TuneA1

;	----
;	BASS
;	----

TuneA2  dw	tripwah2	;Rest48
LoopA2	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7

	dw	tripwah2
	
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7

	dw	Rest24

	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9

	dw	tripwahb2

	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
	dw	Rest24

	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9

 	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
 	dw	A2P7,A2P7,A2P7,A2P7
TestA2	dw	A2P17,A2P17,A2P17,A2P17

	dw	a2p8
	dw	A2P7,A2P7,A2P7		;,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7

	dw	Rest24

	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9
	dw	A2P9,A2P9,A2P9,A2P9

	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7

	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7

	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7

	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7
	dw	A2P7,A2P7,A2P7,A2P7

	dw	rest30
	dw	0,Tunea2

TuneA3  dw	tripwah3	;Rest48
LoopA3	dw	a3p8,a3p8

	dw	tripwah3,rest48
	dw	rest48
	
	dw	a3p8,a3p8
	dw	a3p8
	dw	a3p11
	dw	tripwahb3
	
  	dw	a3p14
	dw	a3p8,a3p8,a3p8,a3p8x
	
	dw	a3p15
TestA3	dw	a3p16
	dw	a3p8
	dw	a3p8,a3p8,a3p8
	
	dw	a3p8,a3p8,a3p8,a3p8,a3p8x2
	dw	rest30
	dw	0,Tunea3
	
TuneA4  dw	tripwah4	;rest48
LoopA4	dw	A4P4

	dw	tripwah4	;rest48
	dw	a4p5
	dw	a4p6
	dw	a4p7
	dw	tripwahb4
	
	dw	a4p8
	dw	a4p7
	dw	a4p4
TestA4	dw	a4p9,a4p9
	dw	a4p10
;	dw	a4p6
	
	dw	a4p7x2
	dw	a4p4,a4p4
	dw	rest30
	dw	0,TuneA4

TuneA5	dw	Rest48
LoopA5	dw	a5p8

	dw	a5p9
	dw	a5p8
	dw	a5p7

	dw	rest48
	
	dw	a5p10
	dw	a5p7
	dw	a5p8
TestA5	dw	a5p11	; ,rest
	dw	a5p8x
	dw	a5p7x
	dw	rest,rest,rest,a5p8x
	dw	rest30
	dw	0,TuneA5

bd	equ	patch1
sn1	equ	patch2
hhc	equ	patch3
bdhh	equ	patch4
hho	equ	patch5
bass	equ	patch8
lead	equ	patch9
lead2	equ	patch13

tripwah	db	lead2,tra,18h-5
	db	l2,gs2,as2,gs2,g2,cs2,ds2
	db	fs2,gs2,fs2,f2,ds2,cs2,ds2,p,l20,ds0
	db	xm

tripwah2	db	lead2,vol,6ah,tra,18h-5
	db	l2,gs1,as1,gs1,g1,cs1,ds1
	db	fs1,gs1,fs1,f1,ds1,cs1,ds1,p,l6,lead,f5,l14,p
	db	xm
	
tripwah3	db	lead2,vol,60h,l2,p,tra,18h-5
	db	l2,gs2,as2,gs2,g2,cs2,ds2
	db	fs2,gs2,fs2,f2,ds2,cs2,ds2,p,l18,ds0
	db	xm
	
tripwah4	db	lead2,vol,60h,l2,p,tra,18h-5
	db	l2,gs1,as1,gs1,g1,cs1,ds1
	db	fs1,gs1,fs1,f1,ds1,cs1,ds1,l6,lead,f5,l14,p
	db	xm
	
tripwahb	db	lead2,tra,18h-5
	db	l2,gs2,as2,gs2,g2,cs2,ds2
	db	fs2,gs2,fs2,f2,ds2,cs2,l12,ds2,l6,fs2,g2
	db	xm
	
tripwahb2	db	lead2,vol,6ah,tra,18h-5
	db	l2,gs1,as1,gs1,g1,cs1,ds1
	db	fs1,gs1,fs1,f1,ds1,cs1,l24,ds1
	db	xm
	
tripwahb3	db	lead2,vol,60h,l2,p,tra,18h-5
	db	gs2,as2,gs2,g2,cs2,ds2
	db	fs2,gs2,fs2,f2,ds2,cs2,l22,ds2
	db	xm
	
tripwahb4	db	lead2,vol,60h,l2,p,tra,18h-5
	db	gs1,as1,gs1,g1,cs1,ds1
	db	fs1,gs1,fs1,f1,ds1,cs1,l22,ds1
	db	xm
	

A1P8	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch2,L6,65
	DB	patch3,L3,65
	DB	patch4,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch2,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,65
	DB	patch2,65
	DB	patch3,L3,65
	DB	patch4,65
	
	DB	patch1,L3,65
	DB	patch2,65
	DB	patch5,L6,65
	DB	patch2,65
	DB	patch3,L3,65
	DB	patch4,65

a1p8x2	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch2,L6,65
	DB	patch3,L3,65
	DB	patch4,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch2,65
	DB	patch3,L3,65,65

a1p8x	DB	patch1,L6,65
	DB	patch3,65
	DB	patch2,65
	DB	patch3,L3,65
	DB	patch4,65
	
	DB	patch1,L3,65
	DB	patch5,65
	DB	patch5,L6,65
	DB	patch2,L3,65,65
	DB	patch4,65,65
	DB	XM

A1P11	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch2,L6,65
	DB	patch3,L3,65
	DB	patch4,L3,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch2,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,65
	DB	patch2,65
	DB	patch3,L3,65
	DB	patch4,65
	
	DB	patch1,65
	DB	patch2,65
	DB	patch5,L6,65
	DB	patch2,65
	DB	patch3,L3,65
	DB	patch4,65

	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch2,L6,65
	DB	patch3,L3,65,P

	DB	L24,P
	db	xm

;BEEP DRUMS
A1P14	DB	patch1,L12,65,65,65,65,65,65,65,65

	DB	L12,65
	DB	patch2,L9,65
	DB	L3,65
	DB	patch1,L12,65
	DB	patch2,L12,65
	
	DB	patch1,L12,65
	DB	patch2,L9,65
	DB	L3,65
	DB	patch1,L3,65
	DB	patch2,L9,65
	DB	L9,65
	DB	L3,65
	db	xm

A1p15	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch2,L6,65
	DB	patch3,L3,65
	DB	patch4,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch2,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65
	
	DB	patch1,L6,65
	DB	patch5,65
	DB	patch1,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65
	
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65
	
	DB	patch1,L6,65
	DB	patch5,65
	DB	patch1,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,l6,65
	DB	patch3,l3,65,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65
	
	DB	patch1,L6,65
	DB	patch5,65
	DB	patch1,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65
	db	xm
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch5,65
	DB	patch1,65
	DB	patch3,L3,65,65
	DB	xm

;TRIP DRUMS
A1P16	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65
	
	DB	patch1,L6,65
	DB	patch5,65
	DB	patch1,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,L3,65,65
	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65

	DB	patch1,L6,65
	DB	patch3,65
	DB	patch1,65
	DB	patch3,L3,65,65
	
	DB	patch1,L6,65
	DB	patch5,65
	DB	patch1,65
	DB	patch3,L3,65,65
	DB	xm

A1P21	DB	patch1,L4,65
	DB	patch3,L2,65,65
	DB	patch2,L4,65
	DB	patch3,L2,65
	DB	patch4,L2,65
	DB	patch1,L4,65
	DB	patch3,L4,65
	DB	patch2,L4,65
	DB	patch3,L2,65,65

	DB	patch1,L4,65
	DB	patch3,L4,65
	DB	patch2,L4,65
	DB	patch3,L2,65
	DB	patch4,L2,65
	
	DB	patch1,L2,65
	DB	patch7,L2,65
	DB	patch5,L4,65
	DB	patch2,L4,65
	DB	patch3,L2,65
	DB	patch4,L2,65
	db	xm

rest30	DB	L30,P
	DB	XM

v1	equ	70h
v2	equ	7ah

A2P7	DB	patch8,vol,v1,L3,63,63,63
	DB	vol,v2,63
	DB	XM
	
a2p8	db	lead2
	db	l2,gs2,as2,gs2,g2,cs2,ds2
	db	fs2,gs2,fs2,f2,ds2,cs2,ds2,p,l20,ds0
	
;	db	l6,p,l12,patch10,c4,c4,c4,l6,c4
	db	l6,p,patch10,l4,c4,ti,p,c4,ti,p,c4,ti,p,l6,c4
	db	c4
;	db	xm	last note of a2p8 is lead in on a2p7
;A2P7
	DB	patch8,vol,v1,L3,63
	DB	vol,v2,63
	DB	XM
	

;GRUFF BASS
A2P9	DB	patch8,vol,v1,L3,70,70,70
	DB	vol,v2,70
	DB	XM

A2P17	DB	patch8,vol,v1,L3,61,61,61
	DB	vol,v2,61

	DB	vol,v1,61,61,61
	DB	vol,v2,61

	DB	vol,v1,61,61,61
	DB	vol,v2,61
	
	DB	vol,v1,61,61,61
	DB	vol,v2,61

	DB	vol,v1,61,61,61
	DB	vol,v2,61

	DB	vol,v1,61,61,61
	DB	vol,v2,61

	DB	vol,v1,61,61,61
	DB	vol,v2,61

	DB	vol,v1,61,61,61
	DB	vol,v2,61
	DB	xm

A2P23	DB	L30,p
	DB	XM

;On this channel patch4(BD+HH) has been replaced by patch3(HH)
;as channel one plays the bassdrum.

A3P8	DB	patch1,L6,68,p
	DB	patch6,65
	DB	patch3,L3,p,65
	DB	patch1,L6,68,p
	DB	patch6,65,p

	DB	patch1,68,p
	DB	patch6,65
	DB	patch3,L3,p,65
	
	DB	patch1,68
	DB	patch6,65
	DB	L6,p
	DB	patch6,65
	DB	patch3,L3,p,65

a3p8x2	DB	patch1,L6,68,p
	DB	patch6,65
	DB	patch3,L3,p,65
	DB	patch1,L6,68,p
	DB	patch6,65,p

a3p8x	DB	patch1,L6,68,p
	DB	patch6,65
	DB	patch3,L3,p,65
	
	DB	patch1,L3,68
	DB	l9,p
	DB	patch6,l3,65,65
	DB	patch3,65,65
	DB	XM
	
A3P11	DB	patch1,L6,68,p
	DB	patch6,65
	DB	patch3,L3,p,65
	DB	patch1,L6,68,p
	DB	patch6,65,p

	DB	patch1,68,p
	DB	patch6,65
	DB	patch3,L3,p,65
	
	DB	patch1,68
	DB	patch6,65
	DB	patch6,l6,p,65
	DB	patch3,L3,p,65

	DB	patch1,L6,68,p
	DB	patch6,65,p

	DB	L24,P
	db	xm
	
A3P14   DB	patch1,L12,68,68,68,68,68,68,68,68
	DB	68
	DB	patch6,L9,65,l3,65
	DB	patch1,L12,68
	DB	patch6,65
	
	DB	patch1,68
	DB	patch6,L9,65,l3,65
	DB	patch1,68
	DB	patch6,L9,65,65
	DB	L3,65
	db	xm

A3p15	DB	patch1,L6,68,p
	DB	patch6,65
	DB	patch3,L3,p,65
	DB	patch1,L6,68
	DB	patch6,p,65,p

	DB	patch1,68,p,68,p
	DB	68,p,68,p

	DB	68,p,68,p
	
	DB	68,p,68,p

	DB	68,p,68,p
	DB	68,p,68,p

	DB	68,p,68,p
	
	DB	68,p,68,p
	DB	68,p,68,p
	DB	68,p,68,p

	DB	68,p,68,p
	
	DB	68,p,68,p

	DB	68,p,68,p
	DB	68,p,68,p
	db	xm
	
A3P16	DB	patch1,L6,68,p,68,p
	DB	68,p,68,p

	DB	68,p,68,p
	
	DB	68,p,68,p

	DB	68,p,68,p
	DB	68,p,68,p

	DB	68,p,68,68
	
	DB	68,p,68,p
	DB	xm
	
;VOICE4

;	patch18 > patch9
;LEAD
A4P4	DB	L6,P
	db	patch9,65,58,60,65,58,60,63
	DB	65,66,68,63,L9,P
	DB	lead2,vol,70h,l3,b0,e1,b0,e1,l2,p,l1,ds1
	DB	patch9,L6,P,65,58,60,65,58,60,63
	DB	L3,P,65,P
	DB	65,L9,66
	DB	l3,66,P,66,L9,68
	DB	l3,66,P,68
	
	DB	L6,P,65,58,60,65,58,60,63
	DB	65,66,68,63,L9,P
	DB	lead2,vol,70h,l3,b0,e1,b0,e1,l2,p,l1,ds1
	DB	patch9,L6,P,65,58,60,65,58,60,63
	DB	L3,P,65,P
	DB	65,L9,66
	DB	L3,66,P,66,L9,68
	DB	L3,66,P,68
	db	xm

	;db	lead2
	;db	l2,gs2,as2,gs2,g2,cs2,ds2
	;db	fs2,gs2,fs2,f2,ds2,cs2,ds2,p,l8,ds0,l12,p

a4p5	DB	patch20,L30,p		
	DB	patch21,L6,p,L24,P,l21,p

        DB	lead2,vol,70h,l3,b0,e1,b0,e1,l2,p,l1,ds1
	db	xm

a4p6	DB	patch9,l6,p,65,58,60,65,58,60,63
	DB	65,66,68,63,L9,P
        DB	lead2,vol,70h,l3,b0,e1,b0,e1,l2,p,l1,ds1
	DB	patch9,L6,P,65,58,60,65,58,60,63
	DB	L3,P,65,P
	DB	65,L9,66
	DB	L3,66,P,66,L9,68
	DB	L3,66,P,68
	
	DB	L6,P,65,58,60,65,58,60,63
	DB	65,66,68,63,L9,P
        DB	lead2,vol,70h,l3,b0,e1,b0,e1,l2,p,l1,ds1
	DB	patch9,L6,P,65,58,60,65,58,60,63
	DB	L3,P,65,P
	DB	65,L9,66,P

	DB	L6,61,63,64
	db	xm
	
a4p7	DB	L9,patch9,60,68,65,59,L6,63,65
	DB	L9,60,69,68,64,L6,61,66

	DB	L9,60,68,65,59,L6,63,65
a4p7x2	DB	L9,patch9,60,69,68,64,L6,61,66
	DB	L9,60,68,65,59,L6,63,65
	DB	L9,60,69,68,64,L6,61,66
	DB	L9,60,68,65,59,L6,63,65
	db	xm
	
a4p7x	db	lead2
	db	l2,gs2,as2,gs2,g2,cs2,ds2
	db	fs2,gs2,fs2,f2,ds2,cs2,l12,ds2,l6,fs2,g2
	db	xm
	
a4p8	db	l6,lead2,vol,70h,ds1,as1,ds1,l3,cs2,ds1,ti,as1,l6,ds1,ds2,as1
        db	ds1,as1,ds1,l3,cs2,ds1,ti,as1,l6,ds1,ds2,as1
        db	ds1,as1,ds1,l3,cs2,ds1,ti,as1,l6,ds1,ds2,as1
        db	ds1,as1,ds1,l3,cs2,ds1,ti,as1,l6,ds1,ds2,as1
        db	ds1,as1,ds1,l3,cs2,ds1,ti,as1,l6,ds1,ds2,as1
        db	ds1,as1,ds1,l3,cs2,ds1,ti,as1,l6,ds1,ds2,as1

	db	patch14,l3,ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4
	DB	L6,patch9,64
	db	xm
	db	l27,p,p,p
	db	xm

a4p9	db	lead2,vol,70h,tra,0
	db	l2,c4,cs4,c4,l3,b3,p,l12,p
	db	l3,p,l9,b3,l2,as3,b3,as3,l6,a3
	db	l2,gs3,a3,gs3,l3,g3,p,l12,p
	db	l3,p,l9,g3,l2,fs3,gs3,fs3,l6,f3
	
	db	l3,cs3,l9,p,cs3,l6,cs3,l9,cs3,l6,cs3,cs4
	db	l12,p,l9,cs4,l6,cs4,l9,cs4,l6,cs4,cs3
	db	xm
	
a4p10	db	lead2,vol,6ah
	db	l2,gs1,as1,gs1,g1,cs1,ds1
	db	fs1,gs1,fs1,f1,ds1,cs1,ds1,p,l6,lead,f5,l14,p
	
;	db	l6,p,l12,patch10,g3,g3,g3,l6,g3
	db	l6,p,patch10,l4,g3,ti,p,g3,ti,p,g3,ti,p,l6,g3
	db	g3
;	db	xm
;a4p6
	DB	patch9,l6,65,58,60,65,58,60,63
	DB	65,66,68,63,L9,P
        DB	lead2,vol,70h,l3,b0,e1,b0,e1,l2,p,l1,ds1
	DB	patch9,L6,P,65,58,60,65,58,60,63
	DB	L3,P,65,P
	DB	65,L9,66
	DB	L3,66,P,66,L9,68
	DB	L3,66,P,68
	
	DB	L6,P,65,58,60,65,58,60,63
	DB	65,66,68,63,L9,P
        DB	lead2,vol,70h,l3,b0,e1,b0,e1,l2,p,l1,ds1
	DB	patch9,L6,P,65,58,60,65,58,60,63
	DB	L3,P,65,P
	DB	65,L9,66,P

	DB	L6,61,63,64
	db	xm
	


a5p7	DB	l3,p,L9,patch9,vol,40h,60,68,65,59,L6,63,65
	DB	L9,60,69,68,64,L6,61,66

	DB	L9,60,68,65,59,L6,63,l3,65
a5p7x	DB	patch9,vol,40h,l3,p
	db	l9,60,69,68,64,L6,61,66
	DB	L9,60,68,65,59,L6,63,65
	DB	L9,60,69,68,64,L6,61,66
	DB	L9,60,68,65,59,L6,63,l3,65
	db	xm
	
a5p8	db	patch14
	db	l3,p,ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3	;,ds4
	db	l6,patch15,cs3,fs3,ds3,gs3   
	
a5p8x	db	l3,patch14,p,ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3	; ,ds4
	db	l6,patch15,cs3,fs3,ds3,gs3   
	db	xm
	
a5p9    db	l18,p,p,patch14,l3,p,ds3,ds4,ds3,p
	db	l3,patch14,ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3,p
	db	ds3,ds4,ds3,p,ds3,ds4,ds3	;,p
	db	xm

a5p10   db	l24,p,p,p,l9,p
	db	lead2,vol,7dh,l3,b0,e1,b0,e1,l2,p,l1,ds1
	db	l24,p,p,p,p
	db	l24,p,p,p,l9,p
	db	lead2,vol,7dh,l3,b0,e1,b0,e1,l2,p,l1,ds1
	
	db	l24,p,p,p,l9,p
	db	lead2,vol,7dh,l3,b0,e1,b0,e1,l2,p,l1,ds1
	db	l24,p,p,p,l9,p
	db	lead2,vol,7dh,l3,b0,e1,b0,e1,l2,p,l1,ds1
	db	xm

a5p11	db	l24,p,p,p,l9,p
	db	L2,p,lead2,vol,70h,l1,b1,l3,e2,b1,e2,l2,p,l1,ds2
	db	l24,p,p
	db	patch9,tra,256-9,vol,76h
	db	l9,as2,l3,as2,d3,ds3,e3
	db	l6,f3,gs3,l3,f3,l6,gs3,as3
	
	db	l24,p,p
	db	l9,p,l3,as2,d3,ds3,e3,f3
	db	ti,gs3,p
        db	L2,p,lead2,vol,70h,l1,b1,l3,e2,b1,e2,l2,p,l1,ds2

	db	l24,p,p
	db	patch9,tra,256-9,vol,76h
	db	l9,as2,l3,as2,d3,ds3,e3
	db	l6,f3,gs3,l3,f3,l6,gs3,as3
	
	db	lead2,vol,60h,l2,p
	db	gs2,as2,gs2,g2,cs2,ds2
	db	fs2,gs2,fs2,f2,ds2,cs2,ds2,p,l18,ds0
	
;	db	l6,p,l12,patch10,f2,f2,f2,l6,f2
	db	l6,p,patch10,l4,f2,ti,p,f2,ti,p,f2,ti,p,l6,f2
	db	f3
;	db	xm
;Rest
	db	l18,p,l24,p,p,p
	db	p,p,p,p
	db	xm
	

Rest24	db	l24,p
	db	xm

Rest48	db	l24,p,p
	db	xm

Rest96	db	l24,p,p,p,p
	db	xm

Rest144	db	l24,p,p,p,p,p,p
	db	xm

Rest	db	l24,p,p,p,p
	db	p,p,p,p
	db	xm

