BT_DEVICE_ID = '00:12:09:25:91:70'
import time
import re
import pprint
import textwrap
import android
import colorsys
from base64 import b64encode

droid = android.Android()
blacklist = []

#Messages must be 160 character long SMS, not MMS, or multipart SMS.
#Multipart messages will be ignored.
#Messages that are plain text are centered and displayed on the helmet.

#Message may include up to two HTML Color codes (A primary and secondary), 3 or 6 digits long.
#Example: #F0F or #3352FF
#Black and white are ignored, and all color codes will have their chroma maximized before display.
#Example: #442244 turns into #FF00FF
#The primay color code is used as is.
#The secondary is adjusted to be at most 120 degrees away from the primary on the HSV circle.
#Color codes are removed from any attached text before it is displayed on the helmet.

#Messages exactly 80 or 160 characters long are treated as ascii art and displayed unchanged.
#A 160 character long message will be shown as two animated frames.
#An 80 character message is shown as a static image.
#The character count is taken without color codes.
#Technically, anything in the GSM 03.38 character set will work for art...
#But not everything has a corresponding character on the LCD screen.

#Don't be afraid to try it, you cant break anything, and if you do, congratulations!

while True:
	
	#kill some time
	time.sleep(1)
	
	#keep connection to suit warm
	while (len(droid.bluetoothActiveConnections().result) == 0):
		print "Reconnecting..."	
		droid.bluetoothConnect('00001101-0000-1000-8000-00805F9B34FB', BT_DEVICE_ID)
		print "Connected..."
		
	#get new messages		
	input = droid.smsGetMessages(True)	
			
	if (len(input.result) != 0):
		
		#wake up and get to work!
		droid.wakeLockAcquirePartial()
		
		newlist = []
		#clean expired events from blacklist		
		for object in blacklist:
			if (object['time'] > time.time()):
				newlist.append(object)
		blacklist = newlist	
		
		count = 0
		#check if new item is in blacklist
		for object in blacklist:
			if (object['value'] == input.result[0]['address']):
				count += 1		
					
		#add new event to the blacklist - 10 second timeout, plus punishment multiplier of message count
		#blacklist.append({'time':time.time() + (10 * (count + 1)),'value':input.result[0]['address']})
		#just blacklist for 10 seconds to avoid multipart messages
		blacklist.append({'time':time.time() + 10,'value':input.result[0]['address']})
		
		if (count > 0):
			#message recieved too quicky or a multipart message or being flooded.  Ignore.
			print "Blocked message from " + input.result[0]['address'] 
			droid.smsMarkMessageRead([input.result[0]['_id']],True)
			droid.wakeLockRelease()
		else:
			#get message body
			body = input.result[0]['body']
								
			initalMatches = []
			#find all hex codes
			initalMatches = re.findall( r'#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})\b' , body)

			#expand 3 character codes into 6 character codes
			expandedMatches = []
			for item in initalMatches:
				if(len(item) == 3):
					expandedMatches.append(item[0]+item[0]+item[1]+item[1]+item[2]+item[2])
				else:
					expandedMatches.append(item)
					
			#convert hex codes to groups of RGB ints
			colorDegrees = []
			for item in expandedMatches:
				colorDegrees.append(colorsys.rgb_to_hsv(float(int(item[0:2],16)),float(int(item[2:4],16)),float(int(item[4:6],16)))[0] * 360)

			#span calculations for multi color mode		
			span = 0 #set span to zero so if we have no secondary colors it clears it
			if (len(colorDegrees) > 1):
			  #find shortest path around circle to get between the starting and ending point
				if((( colorDegrees[1] - colorDegrees[0] + 360) % 360) < 180):
					span = colorDegrees[0] - colorDegrees[1];
					if (span < -180):
						span = span +360;
					elif (span > 180):
						span = span -360;
				else:
					span= colorDegrees[1] - colorDegrees[0];
					if (span < -180):
						span = span +360;
					elif (span > 180):
						span = span -360;
					span=  span*-1;
			  
			#convert span degrees to suit colors
			span = int((span * 16)/15)	
			#limit span changes to 128 suit colors in either direction
			if (span < 0):
				span = max(span,-128)
			else:
				span = min(span,128)

			span = span + 256;
			span = span % 512;

			#checks if we found a primary color, and if so, converts to suit colors (scale of 127*3)
			color = -1 #impossible color	
			if (len(colorDegrees) > 0):	
				color = int((colorDegrees[0] * 384/360 + 384) % 384)
				
			
			cleanString = []
			#clean the string for display
			cleanString = re.sub( r'#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})\b' , '', body).strip()
			
			#filters out control characters from input text
			outputText = ""	
			for character in cleanString:
				if (ord(character) > 31):
					outputText += character			
			
			#send the text	
			if (len(outputText) > 0):	
				
				#special case for a single frame of art
				if (len(outputText) == 80):
					outputText+=outputText
				
				#either a full two frames of art or a long text
				else:
				
					#attempt pretty line wrapping
					wrappedtext = textwrap.wrap(outputText.strip(), 20)
					
					#ugly line wrapping if too long
					if(len(wrappedtext) > 8):
						outputText = outputText.ljust(160)
					else:
						#if message is smaller than one frame, expand to one frame
						while(len(wrappedtext)<4):
							wrappedtext.append(" ")
							if(len(wrappedtext)<4):
								wrappedtext.insert(0," ")
								
						#if message is exactly one frame, double it
						if(len(wrappedtext)==4):
							wrappedtext.extend(wrappedtext)
						
						#if message is shorter than 8 lines, expand it to 8
						if(len(wrappedtext)<8):
							wrappedtext.append(" ")
						if(len(wrappedtext)<8):
							wrappedtext.insert(3," ")
						if(len(wrappedtext)<8):
							wrappedtext.append(" ")	
						
						#pad lines out to be 20 characters long and combine
						outputText = ""
						for line in wrappedtext:
							#center justify text by padding 
							outputText += line.strip().center(20)
						
			print "Data Parsed"
				
			sent=False
			
			while(sent == False):
				
				#connect to suit
				while (len(droid.bluetoothActiveConnections().result) == 0):
					print "Connecting..."
					droid.bluetoothConnect('00001101-0000-1000-8000-00805F9B34FB', BT_DEVICE_ID)
					print "Connected!"
				
				#everything goes in one big try block.  If it fails we need to resend it all anyway.
				try:
				
					#empty incoming buffer before starting
					while(droid.bluetoothReadReady().result):
						droid.bluetoothRead()
							
					#send the color	
					if (color >= 0):	
						#send color			
						droid.bluetoothWriteBinary(b64encode(chr(17)))
						if (color < 128): #pad if data is small
							droid.bluetoothWriteBinary(b64encode(chr(0)))
							droid.bluetoothWriteBinary(b64encode(chr((color << 1) & 0xFE)))
						else:
							droid.bluetoothWriteBinary(b64encode(chr((color >> 6) & 0xFE)))
							droid.bluetoothWriteBinary(b64encode(chr((color << 1) & 0xFE)))	
							
						if (span < 128): #pad if data is small
							droid.bluetoothWriteBinary(b64encode(chr(0)))
							droid.bluetoothWriteBinary(b64encode(chr((span << 1)& 0xFE)))
						else:
							droid.bluetoothWriteBinary(b64encode(chr((span >> 6) & 0xFE)))
							droid.bluetoothWriteBinary(b64encode(chr((span << 1) & 0xFE)))	
							
						#wait one second to recieve ack back
						deadline = time.time() + .5
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						
						#check incoming buffer
						if(droid.bluetoothReadBinary().result is None):
								raise Exception
								
					if (len(outputText) > 0):	
						#send frame
						droid.bluetoothWriteBinary(b64encode(chr(21)))
						for character in outputText[0:80]:
							droid.bluetoothWrite(character)
					
						#wait one second to recieve ack back
						deadline = time.time() + .5
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						
						#check incoming buffer
						if(droid.bluetoothReadBinary().result is None):
								raise Exception
								
						#send frame2
						droid.bluetoothWriteBinary(b64encode(chr(23)))
						for character in outputText[80:160]:  
							droid.bluetoothWrite(character)
						
						#wait one second to recieve ack back
						deadline = time.time() + .5
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						
						#check incoming buffer
						if(droid.bluetoothReadBinary().result is None):
								raise Exception

					#send confirmation request
					droid.bluetoothWriteBinary(b64encode(chr(25)))
					
					color = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in color):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						color.append(droid.bluetoothRead(1).result)
					
					color = float(''.join(color))
					
					span = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in span):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						span.append(droid.bluetoothRead(1).result)
					
					span = float(''.join(span))
						
					fps = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in fps):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						fps.append(droid.bluetoothRead(1).result)
						
					fps = int(''.join(fps))
					
					bpm = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in bpm):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						bpm.append(droid.bluetoothRead(1).result)
						
					bpm = int(''.join(bpm))	
					
					jacketvolts = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in jacketvolts):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						jacketvolts.append(droid.bluetoothRead(1).result)	
						
					jacketvolts = float(''.join(jacketvolts))
					
					discvolts = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in discvolts):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						discvolts.append(droid.bluetoothRead(1).result)	
						
					discvolts = float(''.join(discvolts))
					
					uptime = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in uptime):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						uptime.append(droid.bluetoothRead(1).result)	
						
					uptime = int(''.join(uptime))
					
					beats = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in beats):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						beats.append(droid.bluetoothRead(1).result)	
						
					beats = int(''.join(beats))
					
					lifetimeuptime = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in lifetimeuptime):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						lifetimeuptime.append(droid.bluetoothRead(1).result)	
						
					lifetimeuptime = int(''.join(lifetimeuptime))
					
					lifetimebeats = []
					#wait one second to recieve a line of data back
					deadline = time.time() + .5
					while('\n' not in lifetimebeats):
						while(droid.bluetoothReadReady().result == False):
							if(time.time() > deadline):
								raise Exception
						lifetimebeats.append(droid.bluetoothRead(1).result)	
						
					lifetimebeats = int(''.join(lifetimebeats))
					
				except:
					print "Exception During Sending."
					time.sleep(10)
				else:
					
					sent=True
					
					output = "Confirmed. Status Report:"
					if color == 384:
						output +=  " Color1:#FFFFFF Color2:#------"
					elif color == 385:
						output +=  " Color1:RAINBOW Color2:#------"
					else:
						output += " Color1:#"
						for i in colorsys.hsv_to_rgb(color/384, 1.0, 1.0):
							output += "".join('%02x' % (i*255)).upper()
						
					output += " Color2:#"
					if span == 0:
						output += "------"
					else:
						for i in colorsys.hsv_to_rgb((color + span)/384, 1.0, 1.0):
							output +=  "".join('%02x' % (i*255)).upper()

					output += " FPS:" + str(fps)
				
					if bpm == 1000:
						output += " BPM:-"
					else: 
						output += " BPM:" + str(60000/bpm)
					output += " Suit:{0:2.2f}".format(15.08/759*jacketvolts) + "V"
					output += " Disc:{0:2.2f}".format(11.11/693*discvolts) + "V"
					
					uptime = (uptime / 1000) #convert milliseconds to seconds
	
					hours, time_remainder = divmod(uptime, 3600)
					minutes, time_remainder = divmod(time_remainder, 60)
					seconds = time_remainder
					
					output += " Uptime: %i:%02i:%02i" % (hours, minutes, seconds)
					
					output += " Beats:" + str(beats)
					
					uptime = uptime + (lifetimeuptime * 60) #convert minutes to seconds and add
					
					hours, time_remainder = divmod(uptime, 3600)
					minutes, time_remainder = divmod(time_remainder, 60)
					seconds = time_remainder
					
					output += " Lifetime: %i:%02i:%02i" % (hours, minutes, seconds)
					
					output += " Beats:" + str(beats + lifetimebeats)
					
					print "Message from " + input.result[0]['address']  
					print output
				
					droid.smsMarkMessageRead([input.result[0]['_id']],True)
					droid.smsSend(input.result[0]['address'] ,output)
					print "Ready..."
					droid.wakeLockRelease()
