[org 0]
[bits 16]

	mov	ax, 'H'
	out	0x01, ax
	mov	ax, 'e'
	out	0x01, ax
	mov	ax, 'l'
	out	0x01, ax
	mov	ax, 'l'
	out	0x01, ax
	mov	ax, 'o'
	out	0x01, ax
	mov	ax, '!'
	out	0x01, ax
	hlt
