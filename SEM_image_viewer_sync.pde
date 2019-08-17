import processing.serial.*;

Serial myPort;  // Create object from Serial class
int val;      // Data received from the serial port
int x, y;
static final int WIN_X_SIZE = 1600;
static final int  WIN_Y_SIZE = 1000;

byte[] inBuffer = new byte[32768];

int FrameStartTime, OldFrameStartTime;

int readlen = 0;
int grabFrame = 0;
int savedFrameCount = 0;

void setup() 
{
  
  size(WIN_X_SIZE, WIN_Y_SIZE);
  // I know that the first port in the serial list on my mac
  // is always my  FTDI adaptor, so I open Serial.list()[0].
  // On Windows machines, this generally opens COM1.
  // Open whatever port is the one you're using.
 
 
 try{ myPort = new Serial(this, Serial.list()[1], 9600);}
 catch (RuntimeException e) { println(e.getMessage());}
 

 
  println( myPort.available());
  x = 0;
  y = 0;
  frameRate(120);
}


void draw()
{
  int i, n;
 
   try{  
         readlen = myPort.readBytes(inBuffer);
   }
   catch (RuntimeException e){
     println(e.getMessage());
   }
   
   if (inBuffer != null) 
        {
            //println(readlen);
            for(i=0;i<readlen;i++)
               {
                  if((inBuffer[i] & 0xFF) == 0)
                   {
                      x = 0;
                      y++;
                      for(n=0;n<WIN_X_SIZE;n++)
                        {
                          set(n,y+1,color(255,255,255));
                        }
                   }
                  
                  if((inBuffer[i] & 0xFF) == 1)
                    {
                      if(grabFrame == 0)
                        {
                          x = 0;
                          y = 0;
                        }
                    }
                    if(x <= WIN_X_SIZE && y < WIN_Y_SIZE)
                      {
                         set(x,y,color(inBuffer[i] & 0xFF,inBuffer[i] & 0xFF,inBuffer[i] & 0xFF));
                         x++;                 
                      }
                  }
             }

  //println(frameRate);
}

void keyPressed() {
  String curFileName = "default.png";
  if (key == ' ') 
  {
    if(grabFrame == 0)
      {
         grabFrame = 1;
         println("Stopping acquisition after this frame is complete.");
      }
    else
       {
          grabFrame = 0;
          x = 0;
          y = 200; // Start mid-frame to avoid the user thinking that the acquisition restarted at the actual top
          println("Restarting acquisition");
       }
   }   
   
   if (key == 's')
     {
        if(savedFrameCount < 1000 && savedFrameCount >= 100)
          {
             curFileName = "capture" + savedFrameCount + ".png";
          }
          
          if(savedFrameCount < 100 && savedFrameCount >= 10)
          {
             curFileName = "capture0" + savedFrameCount + ".png";
          }
          
          if(savedFrameCount < 10 && savedFrameCount >= 0)
          {
             curFileName = "capture00" + savedFrameCount + ".png";
          } 
          println("Saved " + curFileName);
         save(curFileName);
         savedFrameCount++;
     }
}


