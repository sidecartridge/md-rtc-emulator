; Firmware loader from cartridge
; (C) 2023-2025 by Diego Parrilla
; License: GPL v3

; Some technical info about the header format https://www.atari-forum.com/viewtopic.php?t=14086

; $FA0000 - CA_MAGIC. Magic number, always $abcdef42 for ROM cartridge. There is a special magic number for testing: $fa52235f.
; $FA0004 - CA_NEXT. Address of next program in cartridge, or 0 if no more.
; $FA0008 - CA_INIT. Address of optional init. routine. See below for details.
; $FA000C - CA_RUN. Address of program start. All optional inits are done before. This is required only if program runs under GEMDOS.
; $FA0010 - CA_TIME. File's time stamp. In GEMDOS format.
; $FA0012 - CA_DATE. File's date stamp. In GEMDOS format.
; $FA0014 - CA_SIZE. Lenght of app. in bytes. Not really used.
; $FA0018 - CA_NAME. DOS/TOS filename 8.3 format. Terminated with 0 .

; CA_INIT holds address of optional init. routine. Bits 24-31 aren't used for addressing, and ensure in which moment by system init prg. will be initialized and/or started. Bits have following meanings, 1 means execution:
; bit 24: Init. or start of cartridge SW after succesfull HW init. System variables and vectors are set, screen is set, Interrupts are disabled - level 7.
; bit 25: As by bit 24, but right after enabling interrupts on level 3. Before GEMDOS init.
; bit 26: System init is done until setting screen resolution. Otherwise as bit 24.
; bit 27: After GEMDOS init. Before booting from disks.
; bit 28: -
; bit 29: Program is desktop accessory - ACC .
; bit 30: TOS application .
; bit 31: TTP

ROM4_ADDR			equ $FA0000
FRAMEBUFFER_ADDR	equ $FA8000
FRAMEBUFFER_SIZE 	equ 8000	; 8Kbytes of a 320x200 monochrome screen
COLS_HIGH			equ 20		; 16 bit columns in the ST
ROWS_HIGH			equ 200		; 200 rows in the ST
BYTES_ROW_HIGH		equ 80		; 80 bytes per row in the ST
PRE_RESET_WAIT		equ $FFFFF
TRANSTABLE			equ $FA1000	; Translation table for high resolution

CMD_NOP				equ 0		; No operation command
CMD_RESET			equ 1		; Reset command
CMD_BOOT_GEM		equ 2		; Boot GEM command
CMD_TERMINAL		equ 3		; Terminal command
CMD_START 			equ 4  		; Continue boot process and emulation


_conterm			equ $484	; Conterm device number

; Constants needed for the commands
RANDOM_TOKEN_ADDR:        equ (ROM4_ADDR + $F000) 	      ; Random token address at $FAF000
RANDOM_TOKEN_SEED_ADDR:   equ (RANDOM_TOKEN_ADDR + 4) 	  ; RANDOM_TOKEN_ADDR + 4 bytes
RANDOM_TOKEN_POST_WAIT:   equ $1        		      	  ; Wait this cycles after the random number generator is ready

SHARED_VARIABLES:     	  equ (RANDOM_TOKEN_ADDR + (16 * 4)); random token + 16*4 bytes to the shared variables area

ROMCMD_START_ADDR:        equ $FB0000					  ; We are going to use ROM3 address
CMD_MAGIC_NUMBER    	  equ ($ABCD) 					  ; Magic number header to identify a command
														  ; Used to store the system settings
; App commands for the terminal
APP_TERMINAL 				equ $0 ; The terminal app

; App terminal commands
APP_TERMINAL_START   		equ $0 ; Start terminal command
APP_TERMINAL_KEYSTROKE 		equ $1 ; Keystroke command



	include inc/sidecart_macros.s
	include inc/tos.s

; Macros
; XBIOS Vsync wait
vsync_wait          macro
					move.w #37,-(sp)
					trap #14
					addq.l #2,sp
                    endm    

; XBIOS GetRez
; Return the current screen resolution in D0
get_rez				macro
					move.w #4,-(sp)
					trap #14
					addq.l #2,sp
					endm

; XBIOS Get Screen Base
; Return the screen memory address in D0
get_screen_base		macro
					move.w #2,-(sp)
					trap #14
					addq.l #2,sp
					endm

; Check the left or right shift key. If pressed, exit.
check_shift_keys	macro
					move.w #-1, -(sp)			; Read all key status
					move.w #$b, -(sp)			; BIOS Get shift key status
					trap #13
					addq.l #4,sp

					btst #1,d0					; Left shift skip and boot GEM
					bne boot_gem

					btst #0,d0					; Right shift skip and boot GEM
					bne boot_gem

					endm

; Check the keys pressed
check_keys			macro

					gemdos	Cconis,2		; Check if a key is pressed
					tst.l d0
					beq .\@no_key

					gemdos	Cnecin,2		; Read the key pressed

					cmp.b #27, d0		; Check if the key is ESC
					beq .\@esc_key	; If it is, send terminal command

					move.l d0, d3
					send_sync APP_TERMINAL_KEYSTROKE, 4

					bra .\@no_key
.\@esc_key:
					send_sync APP_TERMINAL_START, 0

.\@no_key:

					endm

check_commands		macro
					move.l (FRAMEBUFFER_ADDR + FRAMEBUFFER_SIZE), d6	; Store in the D6 register the remote command value
					cmp.l #CMD_RESET, d6		; Check if the command is a reset
					beq .reset					; If it is, reset the computer
					cmp.l #CMD_BOOT_GEM, d6		; Check if the command is to boot GEM
					beq boot_gem				; If it is, boot GEM
					cmp.l #CMD_START, d6		; Check if the command is to continue booting
					beq rom_function			; If it is, continue booting with the emulation

					; If we are here, the command is a NOP
					; If the command is a NOP, check the keys to send terminal commands
					check_keys
.\@bypass:
					endm

	section

;Rom cartridge

	org ROM4_ADDR

	dc.l $abcdef42 					; magic number
first:
;	dc.l second
	dc.l 0
	dc.l $08000000 + pre_auto		; After GEMDOS init (before booting from disks)
	dc.l 0
	dc.w GEMDOS_TIME 				;time
	dc.w GEMDOS_DATE 				;date
	dc.l end_pre_auto - pre_auto
	dc.b "TERM",0
    even

pre_auto:
; Get the screen memory address to display
	get_screen_base
	move.l d0, a6				; Save the screen memory address in A6

; Enable bconin to return shift key status
	or.b #%1000, _conterm.w

; Get the resolution of the screen
	get_rez
	cmp.w #2, d0				; Check if the resolution is 640x400 (high resolution)
	beq .print_loop_high		; If it is, print the message in high resolution

.print_loop_low:
	vsync_wait

; We must move from the cartridge ROM to the screen memory to display the messages
	move.l a6, a0				; Set the screen memory address in a0
	move.l #FRAMEBUFFER_ADDR, a1			; Set the cartridge ROM address in a1
	move.l #((FRAMEBUFFER_SIZE / 2) -1), d0			; Set the number of words to copy
.copy_screen_low:
	move.w (a1)+ , d1			; Copy a word from the cartridge ROM
	move.w d1, d2				; Copy the word to d2
	swap d2						; Swap the bytes
	move.w d1, d2				; Copy the word to d2
	move.l d2, (a0)+			; Copy the word to the screen memory
	move.l d2, (a0)+			; Copy the word to the screen memory
	dbf d0, .copy_screen_low    ; Loop until all the message is copied

; Check the different commands and the keyboard
	check_commands

	bra .print_loop_low		; Continue printing the message

.print_loop_high:
	vsync_wait

; We must move from the cartridge ROM to the screen memory to display the messages
	move.l a6, a1				; Set the screen memory address in a1
	move.l a6, a2
	lea BYTES_ROW_HIGH(a2), a2	; Move to the next line in the screen
	move.l #FRAMEBUFFER_ADDR, a0		; Set the cartridge ROM address in a0
	move.l #TRANSTABLE, a3		; Set the translation table in a3
	move.l #(ROWS_HIGH -1), d0	; Set the number of rows to copy - 1
.copy_screen_row_high:
	move.l #(COLS_HIGH -1), d1	; Set the number of columns to copy - 1 
.copy_screen_col_high:
	move.w (a0)+ , d2			; Copy a word from the cartridge ROM
	move.w d2, d3				; Copy the word to d3
	and.w #$FF00, d3			; Mask the high byte
	lsr.w #7, d3				; Shift the high byte 7 bits to the right
	move.w (a3, d3.w), d4		; Translate the high byte
	swap d4						; Swap the words

	and.w #$00FF, d2			; Mask the low byte
	add.w d2, d2				; Double the low byte
	move.w (a3, d2.w), d4		; Translate the low byte

	move.l d4, (a1)+			; Copy the word to the screen memory
	move.l d4, (a2)+			; Copy the word to the screen memory

	dbf d1, .copy_screen_col_high   ; Loop until all the message is copied

	lea BYTES_ROW_HIGH(a1), a1	; Move to the next line in the screen
	lea BYTES_ROW_HIGH(a2), a2	; Move to the next line in the screen

	dbf d0, .copy_screen_row_high   ; Loop until all the message is copied

; Check the different commands and the keyboard
	check_commands

	bra .print_loop_high		; Continue printing the message
	
.reset:
	; Copy the reset code out of the ROM because it is going to dissapear!
    move.l #.end_reset_code_in_stack - .start_reset_code_in_stack, d6
    lea -(.end_reset_code_in_stack - .start_reset_code_in_stack)(sp), sp
    move.l sp, a2
    move.l sp, a3
    lea .start_reset_code_in_stack, a1    ; a1 points to the start of the code in ROM
    lsr.w #2, d6
    subq #1, d6
.copy_reset_code:
    move.l (a1)+, (a2)+
    dbf d6, .copy_reset_code
	jmp (a3)

	even
.start_reset_code_in_stack:
    move.l #PRE_RESET_WAIT, d6
.wait_me:
    subq.l #1, d6           ; Decrement the outer loop
    bne.s .wait_me          ; Wait for the timeout

	clr.l $420.w			; Invalidate memory system variables
	clr.l $43A.w
	clr.l $51A.w
	move.l $4.w, a0			; Now we can safely jump to the reset vector
	jmp (a0)
	nop
.end_reset_code_in_stack:

boot_gem:
	; If we get here, continue loading GEM
    rts

; SidecarTridge Multidevice Real Time Clock (RTC) Emulator
; (C) 2023-24-25 by Diego Parrilla
; License: GPL v3

; Emulate a Real Time Clock from the SidecarT

; Bootstrap the code in ASM

; CONSTANTS
APP_RTCEMUL             equ $0300                           ; MSB is the app code. RTC is $03
CMD_TEST_NTP            equ ($0 + APP_RTCEMUL)              ; Command code to ping to the Sidecart
CMD_READ_DATETME        equ ($1 + APP_RTCEMUL)              ; Command code to read the date and time from the Sidecart
CMD_SAVE_VECTORS        equ ($2 + APP_RTCEMUL)              ; Command code to save the vectors in the Sidecart
CMD_REENTRY_LOCK        equ ($3 + APP_RTCEMUL)              ; Command code to lock the reentry to XBIOS in the Sidecart
CMD_REENTRY_UNLOCK      equ ($4 + APP_RTCEMUL)              ; Command code to unlock the reentry to XBIOS in the Sidecart
CMD_SET_SHARED_VAR      equ ($5 + APP_RTCEMUL)              ; Command code to set a shared variable in the Sidecart
RTCEMUL_NTP_SUCCESS     equ (RANDOM_TOKEN_SEED_ADDR + 4)    ; Magic number to identify a successful NTP query
RTCEMUL_DATETIME_BCD    equ (RTCEMUL_NTP_SUCCESS + 4)      ; ntp_success + 4 bytes
RTCEMUL_DATETIME_MSDOS  equ (RTCEMUL_DATETIME_BCD + 8)     ; datetime_bcd + 8 bytes
RTCEMUL_OLD_XBIOS       equ (RTCEMUL_DATETIME_MSDOS + 8)   ; datetime_msdos + 8 bytes
RTCEMUL_REENTRY_TRAP    equ (RTCEMUL_OLD_XBIOS + 4)        ; old_bios + 4 bytes
RTCEMUL_Y2K_PATCH       equ (RTCEMUL_REENTRY_TRAP + 4)     ; reentry_trap + 4 byte
RTCEMUL_SHARED_VARIABLES equ (RTCEMUL_Y2K_PATCH + 8)       ; y2k_patch + 8 bytes

XBIOS_TRAP_ADDR         equ $b8                             ; TRAP #14 Handler (XBIOS)
_longframe      equ $59e    ; Address of the long frame flag. If this value is 0 then the processor uses short stack frames, otherwise it uses long stack frames.

; Send a synchronous command to the Sidecart setting the reentry flag for the next XBIOS calls
; inside our trapped XBIOS calls. Should be always paired with reentry_xbios_unlock
reentry_xbios_lock	macro
                    movem.l d0-d7/a0-a6,-(sp)            ; Save all registers
                    send_sync CMD_REENTRY_LOCK,0         ; Command code to lock the reentry
                    movem.l (sp)+,d0-d7/a0-a6            ; Restore all registers
                	endm

; Send a synchronous command to the Sidecart clearing the reentry flag for the next XBIOS calls
; inside our trapped XBIOS calls. Should be always paired with reentry_xbios_lock
reentry_xbios_unlock  macro
                    movem.l d0-d7/a0-a6,-(sp)            ; Save all registers
                    send_sync CMD_REENTRY_UNLOCK,0       ; Command code to unlock the reentry
                    movem.l (sp)+,d0-d7/a0-a6            ; Restore all registers
                	endm

rom_function:
; Get information about the hardware
	wait_sec
    bsr detect_hw
    bsr get_tos_version
_ntp_ready:
    send_sync CMD_READ_DATETME,0         ; Command code to read the date and time
    tst.w d0                            ; 0 if no error
    bne _exit_timemout                   ; The RP2040 is not responding, timeout now

_set_vectors:
    tst.l RTCEMUL_Y2K_PATCH
    beq.s _set_vectors_ignore

; We don't need to fix Y2K problem in EmuTOS
; Save the old XBIOS vector in RTCEMUL_OLD_XBIOS and set our own vector
    bsr save_vectors
    tst.w d0
    bne _exit_timemout

_set_vectors_ignore:
    pea RTCEMUL_DATETIME_BCD            ; Buffer should have a valid IKBD date and time format
    move.w #6, -(sp)                    ; Six bytes plus the header = 7 bytes
    move.w #25, -(sp)                   ; 
    trap #14
    addq.l #8, sp

    move.l RTCEMUL_DATETIME_MSDOS, d0
    bsr set_datetime
    tst.w d0
    bne _exit_timemout

	move.w #23,-(sp)                    ; gettime from XBIOS
	trap #14
	addq.l #2,sp

    tst.l RTCEMUL_Y2K_PATCH
    beq.s _ignore_y2k
    add.l #$3c000000,d0                 ; +30 years to guarantee the Y2K problem works in all TOS versions
_ignore_y2k:

    move.l d0, -(sp)                    ; Save the date and time in MSDOS format
    move.w #22,-(sp)                    ; settime with XBIOS
    trap #14
    addq.l #6, sp

    rts

_exit_timemout:
    asksil error_sidecart_comm_msg
    rts

save_vectors:
    move.l XBIOS_TRAP_ADDR.w,d3          ; Address of the old XBIOS vector
    send_sync CMD_SAVE_VECTORS,4         ; Send the command to the Sidecart
    tst.w d0                            ; 0 if no error
    bne.s _read_timeout                 ; The RP2040 is not responding, timeout now

    ; Now we have the XBIOS vector in RTCEMUL_OLD_XBIOS
    ; Now we can safely change it to our own vector
    move.l #custom_xbios,XBIOS_TRAP_ADDR.w    ; Set our own vector

    rts

_read_timeout:
    moveq #-1, d0
    rts

custom_xbios:
    btst #0, RTCEMUL_REENTRY_TRAP      ; Check if the reentry is locked
    beq.s _custom_bios_trapped         ; If the bit is active, we are in a reentry call. We need to exec_old_handler the code

    move.l RTCEMUL_OLD_XBIOS, -(sp) ; if not, continue with XBIOS call
    rts 

_custom_bios_trapped:
    btst #5, (sp)                    ; Check if called from user mode
    beq.s _user_mode                 ; if so, do correct stack pointer
_not_user_mode:
    move.l sp,a0                     ; Move stack pointer to a0
    bra.s _check_cpu
_user_mode:
    move.l usp,a0                    ; if user mode, correct stack pointer
    subq.l #6,a0
;
; This code checks if the CPU is a 68000 or not
;
_check_cpu:
    tst.w _longframe                ; Check if the CPU is a 68000 or not
    beq.s _notlong
_long:
    addq.w #2, a0                   ; Correct the stack pointer parameters for long frames 
_notlong:
    cmp.w #23,6(a0)                 ; is it XBIOS call 23 / getdatetime?
    beq.s _getdatetime              ; if yes, go to our own routine
    cmp.w #22,6(a0)                 ; is it XBIOS call 22 / setdatetime?
    beq.s _setdatetime              ; if yes, go to our own routine

_continue_xbios:
    move.l RTCEMUL_OLD_XBIOS, -(sp) ; if not, continue with XBIOS call
    rts 

; Adjust the time when reading to compensate for the Y2K problem
; We should not tap this call for EmuTOS
_getdatetime:
    reentry_xbios_lock
	move.w #23,-(sp)
	trap #14
	addq.l #2,sp
	add.l #$3c000000,d0 ; +30 years for all TOS except EmuTOS
    reentry_xbios_unlock
	rte

; Adjust the time when setting to compensate for the Y2K problem
; We should not tap this call for TOS 2.06 and EmuTOS
_setdatetime:
	sub.l #$3c000000,8(a0)
    bra.s _continue_xbios

; Get the date and time from the RP2040 and set the IKBD information
; d0.l : Date and time in MSDOS format
set_datetime:
    move.l d0, d7

    swap d7

	move.w d7,-(sp)
	move.w #$2d,-(sp)                   ; settime with GEMDOS
	trap #1
	addq.l #4,sp
    tst.w d0
    bne.s _exit_set_time

	swap d7

	move.w d7,-(sp)
	move.w #$2b,-(sp)                   ; settime with GEMDOS  
	trap #1
	addq.l #4,sp
    tst.w d0
    bne.s _exit_set_time

    ; And we are done!
    moveq #0, d0
    rts
_exit_set_time:
    moveq #-1, d0
    rts

        even
error_sidecart_comm_msg:
        dc.b	$d,$a,"Communication error. Press reset.",$d,$a,0

        even
        dc.l $FFFFFFFF
rom_function_end:


; Shared functions included at the end of the file
; Don't forget to include the macros for the shared functions at the top of file
    include "inc/sidecart_functions.s"

end_pre_auto:
	even
	dc.l 0