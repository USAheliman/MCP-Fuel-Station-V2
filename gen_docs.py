#!/usr/bin/env python3
"""Generate updated circuit documentation PDFs for MCP Fuel Station v2.
Changes vs Rev 2.0:
  - Display: ST7735S 1.8" -> ST7796S 3.5" IPS 480x320
  - GT911 capacitive touch added: SDA=GPIO47, SCL=GPIO41, INT=GPIO42 (Wire1)
  - GPIO41/42/47 no longer spare; GPIO34 does not exist on DevKitC-1 N16R8
"""
from fpdf import FPDF
import os

OUT = r"c:\Users\Mehul\Projects\Github\MCP-FuelStation-V2\Circuit Diagram"

# Palette
GREEN_BG  = (198,239,206); GREEN_FG  = (0,97,0)
ORANGE_BG = (255,235,156); ORANGE_FG = (156,87,0)
RED_BG    = (255,199,206); RED_FG    = (156,0,6)
GREY_BG   = (220,220,220); GREY_FG   = (89,89,89)
NAVY      = (31,73,125);   WHITE     = (255,255,255)
BLUE_HDR  = (68,114,196);  TBL_HDR   = (189,215,238)
ALT       = (242,248,255); BLACK     = (0,0,0)

# ═══════════════════════════════════════════════════════
# PIN LIST
# ═══════════════════════════════════════════════════════
class PinList(FPDF):
    def header(self):
        self.set_fill_color(*NAVY)
        self.rect(0,0,210,12,'F')
        self.set_font('Helvetica','B',10)
        self.set_text_color(*WHITE)
        self.set_xy(0,2)
        self.cell(0,8,'MCP Fuel Station v2  -  ESP32-S3 DevKitC-1 N16R8  -  Complete Pin Reference',align='C')
        self.set_text_color(*BLACK)

    def footer(self):
        self.set_y(-8)
        self.set_font('Helvetica','I',7)
        self.set_text_color(128,128,128)
        self.cell(0,4,f'MCP Fuel Station v2 - ESP32-S3 N16R8 - Rev 3.0  |  Page {self.page_no()}',align='C')
        self.set_text_color(*BLACK)

    def sec(self, title):
        self.set_fill_color(*BLUE_HDR)
        self.set_text_color(*WHITE)
        self.set_font('Helvetica','B',8.5)
        self.cell(0,6,f'  {title}',border=0,fill=True,new_x='LMARGIN',new_y='NEXT')
        self.set_text_color(*BLACK)
        self.ln(1)

    def tbl_hdr(self, cols):
        self.set_fill_color(*TBL_HDR)
        self.set_text_color(*NAVY)
        self.set_font('Helvetica','B',7.5)
        for lbl,w in cols:
            self.cell(w,5.5,lbl,border=1,fill=True,align='C')
        self.ln()
        self.set_text_color(*BLACK)

    def gpio_row(self, gpio, direction, connected, signal, notes, scheme='green', alt=False):
        bgs = {'green':GREEN_BG,'orange':ORANGE_BG,'red':RED_BG,'grey':GREY_BG,'white':(WHITE if not alt else ALT)}
        bg = bgs.get(scheme, ALT if alt else WHITE)
        self.set_fill_color(*bg)
        self.set_font('Helvetica','B',7.5); self.cell(18,5,gpio,border=1,fill=True,align='C')
        self.set_font('Helvetica','',7.5)
        self.cell(18,5,direction,border=1,fill=True,align='C')
        self.cell(54,5,connected,border=1,fill=True)
        self.cell(20,5,signal,border=1,fill=True,align='C')
        self.cell(68,5,notes,border=1,fill=True)
        self.set_fill_color(*WHITE)
        self.cell(12,5,'[]',border=1,fill=True,align='C')
        self.ln()

def pin_list():
    pdf = PinList(orientation='P',unit='mm',format='A4')
    pdf.set_margins(10,15,10)
    pdf.set_auto_page_break(True,margin=12)
    pdf.add_page()

    pdf.set_font('Helvetica','',8); pdf.set_text_color(100,100,100)
    pdf.set_xy(10,14); pdf.cell(0,5,'Print and check off each connection during build',align='C')
    pdf.set_text_color(*BLACK); pdf.ln(6)

    pdf.set_font('Helvetica','B',8)
    pdf.set_fill_color(*TBL_HDR); pdf.set_text_color(*NAVY)
    pdf.cell(0,5.5,'  COLOUR KEY',border=1,fill=True,new_x='LMARGIN',new_y='NEXT')
    pdf.set_text_color(*BLACK)
    w = 190/4
    for lbl,bg in [('Green = In use',GREEN_BG),('Orange = Handle with care',ORANGE_BG),
                    ('Red = Reserved / do not use',RED_BG),('Grey = Spare',GREY_BG)]:
        pdf.set_fill_color(*bg); pdf.set_font('Helvetica','',8)
        pdf.cell(w,5.5,f'  {lbl}',border=1,fill=True)
    pdf.ln(); pdf.ln(3)

    pdf.sec('GPIO Pin Assignments')
    cols = [('GPIO',18),('Direction',18),('Connected to',54),('Signal',20),('Notes',68),('Checked',12)]
    pdf.tbl_hdr(cols)

    rows = [
        ('GPIO0','INPUT','Boot strapping pin','BOOT','Do not pull LOW at boot','orange'),
        ('GPIO1','INPUT','Battery voltage divider','ADC','100k/33k -> 3.13V max at 12.6V','green'),
        ('GPIO2','OUTPUT','BTS7960 RPWM','PWM','Fill direction 0-255','green'),
        ('GPIO3','OUTPUT','Passive piezo buzzer','PWM','ledcWriteTone()','green'),
        ('GPIO4','INPUT','Fill flow sensor','PULSE','7.5k/15k divider 3.33V','green'),
        ('GPIO5','INPUT','Drain flow sensor','PULSE','7.5k/15k divider 3.33V','green'),
        ('GPIO6','INPUT','Tank full sensor','DIGITAL','7.5k/15k divider 3.33V','green'),
        ('GPIO7','INPUT','Tank sensor detect pin 4','DIGITAL','15k pull-up to 3.3V','green'),
        ('GPIO8','OUTPUT','BTS7960 R_EN','DIGITAL','Hold HIGH to enable','green'),
        ('GPIO9','OUTPUT','BTS7960 LPWM','PWM','Drain direction 0-255','green'),
        ('GPIO10','OUTPUT','BTS7960 L_EN','DIGITAL','Hold HIGH to enable','green'),
        ('GPIO11','OUTPUT','TFT SCL (SPI clock)','SPI','SPI2 SCK','green'),
        ('GPIO12','OUTPUT','TFT SDA (SPI MOSI)','SPI','SPI2 MOSI','green'),
        ('GPIO13','OUTPUT','TFT CS (chip select)','SPI','Active LOW','green'),
        ('GPIO14','OUTPUT','TFT DC (data/command)','DIGITAL','HIGH=data LOW=cmd','green'),
        ('GPIO15','OUTPUT','TFT RES (reset)','DIGITAL','Active LOW','green'),
        ('GPIO16','I/O','Pololu A pin via D3 1N4148','DIGITAL','Pulls A LOW + reads SW timing','green'),
        ('GPIO17','OUTPUT','Pololu OFF pin','DIGITAL','Pulse HIGH = forced shutdown','green'),
        ('GPIO18','I/O','I2C SDA (Wire0)','I2C','DS3231 + ABP2 (4.7k pull-up)','green'),
        ('GPIO19','I/O','USB D-  RESERVED','USB','Keep free for USB','red'),
        ('GPIO20','I/O','USB D+  RESERVED','USB','Keep free for USB','red'),
        ('GPIO21','INPUT','Encoder DT (direction)','DIGITAL','Read after CLK interrupt','green'),
        ('GPIO26','I/O','Internal flash - do not use','-','Do not connect','red'),
        ('GPIO27','I/O','Internal flash - do not use','-','Do not connect','red'),
        ('GPIO28','I/O','Internal flash - do not use','-','Do not connect','red'),
        ('GPIO29','I/O','Internal flash - do not use','-','Do not connect','red'),
        ('GPIO30','I/O','Internal flash - do not use','-','Do not connect','red'),
        ('GPIO31','I/O','Internal flash - do not use','-','Do not connect','red'),
        ('GPIO32','I/O','Internal flash - do not use','-','Do not connect','red'),
        ('GPIO33','I/O','Internal flash - do not use','-','Do not connect','red'),
        ('GPIO35','I/O','PSRAM RESERVED','PSRAM','Internal - do not use','red'),
        ('GPIO36','I/O','PSRAM RESERVED','PSRAM','Internal - do not use','red'),
        ('GPIO37','I/O','PSRAM RESERVED','PSRAM','Internal - do not use','red'),
        ('GPIO38','I/O','Spare','-','','grey'),
        ('GPIO39','I/O','I2C SCL (Wire0)','I2C','DS3231 + ABP2 (4.7k pull-up)','green'),
        ('GPIO40','INPUT','Encoder CLK (clock)','DIGITAL','Interrupt driven','green'),
        ('GPIO41','I/O','Touch TP_SCL  (Wire1 / I2C1)','I2C','GT911 capacitive touch controller','green'),
        ('GPIO42','INPUT','Touch TP_INT  (interrupt)','DIGITAL','GT911 interrupt - INT floats HIGH at power-up','green'),
        ('GPIO43','OUTPUT','UART0 TX','UART','USB serial / programming','green'),
        ('GPIO44','INPUT','UART0 RX','UART','USB serial / programming','green'),
        ('GPIO45','I/O','Strapping pin','BOOT','Avoid if possible','orange'),
        ('GPIO46','INPUT','Strapping pin (input only)','BOOT','Avoid - input only','orange'),
        ('GPIO47','I/O','Touch TP_SDA  (Wire1 / I2C1)','I2C','GT911 capacitive touch controller','green'),
        ('GPIO48','OUTPUT','TFT BL (backlight PWM)','PWM','PWM brightness 0-255','green'),
    ]
    for i,r in enumerate(rows):
        pdf.gpio_row(*r, alt=(i%2==0))

    pdf.ln(3); pdf.sec('I2C Bus 0 - Wire0  (GPIO18 SDA / GPIO39 SCL)  -  DS3231 RTC + Honeywell ABP2')
    pdf.tbl_hdr([('Device',62),('I2C Address',28),('Power',18),('Pull-ups',28),('Notes',54)])
    for i,r in enumerate([
        ('DS3231 RTC module','0x68','3.3V','4.7k on bus','Remove charge resistor if using CR2032'),
        ('Honeywell ABP2 pressure','0x28','3.3V','4.7k on bus','0-4 bar gauge, barbed port to fuel line'),
    ]):
        bg = ALT if i%2==0 else WHITE
        pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[62,28,18,28,54])):
            pdf.set_font('Helvetica','B'if j==0 else'',7.5)
            pdf.cell(w,5,v,border=1,fill=True)
        pdf.ln()

    pdf.ln(3); pdf.sec('I2C Bus 1 - Wire1  (GPIO47 SDA / GPIO41 SCL)  -  GT911 Capacitive Touch  [NEW]')
    pdf.tbl_hdr([('Device',62),('I2C Address',28),('Power',18),('Pull-ups',28),('Notes',54)])
    pdf.set_fill_color(*GREEN_BG)
    for j,(v,w) in enumerate(zip(
        ('GT911 touch controller','0x5D','3.3V','Internal on FPC',
         'INT floats HIGH on power-up -> default addr 0x5D. No RST wired.'),
        [62,28,18,28,54])):
        pdf.set_font('Helvetica','B'if j==0 else'',7.5)
        pdf.cell(w,5,v,border=1,fill=True)
    pdf.ln()
    pdf.set_font('Helvetica','I',7); pdf.set_text_color(80,80,80)
    pdf.cell(0,4,'  Poll every 50ms unconditionally. Status register 0x814E. Must clear buffer flag on BOTH touch-down AND lift events.',new_x='LMARGIN',new_y='NEXT')
    pdf.set_text_color(*BLACK)

    pdf.ln(3); pdf.sec('SPI2 TFT Display - ST7796S 3.5" IPS 480x320  (GPIO11-15, GPIO48)')
    pdf.tbl_hdr([('TFT Pin',22),('GPIO',20),('Signal',28),('Notes',100),('Checked',20)])
    disp = [('GND','--','GND','Common ground'),('VCC','--','3.3V','From ESP32 3V3 pin'),
            ('SCL','GPIO11','SPI SCK','SPI2 clock'),('SDA','GPIO12','SPI MOSI','SPI2 data'),
            ('RES','GPIO15','Reset','Active LOW'),('DC','GPIO14','Data/Command','HIGH=data  LOW=cmd'),
            ('CS','GPIO13','Chip select','Active LOW'),('BL','GPIO48','Backlight','PWM brightness 0-255')]
    for i,r in enumerate(disp):
        bg = ALT if i%2==0 else WHITE; pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[22,20,28,100])):
            pdf.set_font('Helvetica','B'if j==0 else'',7.5)
            pdf.cell(w,5,v,border=1,fill=True)
        pdf.set_fill_color(*WHITE); pdf.cell(20,5,'[]',border=1,fill=True,align='C')
        pdf.ln()
    pdf.set_font('Helvetica','I',7); pdf.set_text_color(80,80,80)
    pdf.cell(0,4,'  Library: TFT_eSPI (Bodmer)  |  Driver: ST7796_DRIVER  |  USE_FSPI_PORT=1  |  setRotation(1) landscape  |  SPI_FREQUENCY=20MHz',new_x='LMARGIN',new_y='NEXT')
    pdf.set_text_color(*BLACK)

    pdf.ln(3); pdf.sec('Capacitive Touch Controller - GT911  (Wire1: GPIO47 SDA / GPIO41 SCL / GPIO42 INT)  [NEW]')
    pdf.tbl_hdr([('Signal',22),('GPIO',20),('Direction',24),('Notes',114),('Checked',10)])
    touch = [
        ('TP_SDA','GPIO47','I/O','Wire1 I2C data. I2C address 0x5D (INT pin floats HIGH at power-up)','green'),
        ('TP_SCL','GPIO41','OUTPUT','Wire1 I2C clock 400kHz','green'),
        ('TP_INT','GPIO42','INPUT','Interrupt output. Stays LOW after init without RST. Poll unconditionally.','green'),
        ('TP_RST','--','--','Not connected. GT911 uses default address 0x5D.','white'),
        ('VCC','--','--','3.3V supplied via display FPC connector','white'),
        ('GND','--','--','Common ground','white'),
    ]
    for i,r in enumerate(touch):
        bg = GREEN_BG if r[4]=='green' else (ALT if i%2==0 else WHITE)
        pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[22,20,24,114])):
            pdf.set_font('Helvetica','B'if j<2 else'',7.5)
            pdf.cell(w,5,v,border=1,fill=True)
        pdf.set_fill_color(*WHITE); pdf.cell(10,5,'[]',border=1,fill=True,align='C')
        pdf.ln()

    pdf.ln(3); pdf.sec('Rotary Encoder KY-040 style  (GPIO16, GPIO21, GPIO40)')
    pdf.tbl_hdr([('Encoder Pin',30),('GPIO',20),('Function',45),('Notes',75),('Checked',20)])
    for i,r in enumerate([
        ('GND','--','GND','Common ground'),
        ('+','--','3.3V  NOT 5V','Power from 3.3V only - avoids 5V on GPIO'),
        ('SW','GPIO16','Pololu A via D2 1N4148','Via 1N4148 OR gate - GND toggles power'),
        ('DT','GPIO21','Encoder direction','Read after CLK interrupt'),
        ('CLK','GPIO40','Encoder clock','Interrupt driven, debounce 5ms'),
    ]):
        bg = ALT if i%2==0 else WHITE; pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[30,20,45,75])):
            pdf.set_font('Helvetica','B'if j==0 else'',7.5); pdf.cell(w,5,v,border=1,fill=True)
        pdf.set_fill_color(*WHITE); pdf.cell(20,5,'[]',border=1,fill=True,align='C'); pdf.ln()

    pdf.ln(3); pdf.sec('BTS7960 43A H-Bridge Motor Driver - CN10 6-pin Dupont (off-board on heatsink)')
    pdf.tbl_hdr([('BTS7960 Pin',28),('GPIO / Rail',25),('Function',40),('Notes',77),('Checked',20)])
    for i,r in enumerate([
        ('RPWM','GPIO2','Fill direction PWM','0-255 PWM'),
        ('R_EN','GPIO8','Right enable','Hold HIGH in firmware'),
        ('LPWM','GPIO9','Drain direction PWM','0-255 PWM'),
        ('L_EN','GPIO10','Left enable','Hold HIGH in firmware'),
        ('VCC','5V rail','Logic power','From 5V regulator'),
        ('GND','GND rail','Ground','Common ground'),
        ('B+','VBAT+','Motor power +','20A inline fuse mandatory'),
        ('B-','GND rail','Motor power -','Common ground'),
        ('M+','Motor','Motor output +','2-pin bullet connector CN7'),
        ('M-','Motor','Motor output -','2-pin bullet connector CN7'),
    ]):
        bg = ALT if i%2==0 else WHITE; pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[28,25,40,77])):
            pdf.set_font('Helvetica','B'if j==0 else'',7.5); pdf.cell(w,5,v,border=1,fill=True)
        pdf.set_fill_color(*WHITE); pdf.cell(20,5,'[]',border=1,fill=True,align='C'); pdf.ln()

    pdf.ln(3); pdf.sec('Pololu SV Mini Power Switch')
    pdf.tbl_hdr([('Pololu Pin',30),('Connected to',50),('Notes',90),('Checked',20)])
    for i,r in enumerate([
        ('VIN','VBAT+ (direct)','Built-in reverse voltage protection'),
        ('GND','GND rail','Common ground'),
        ('VOUT','MP1584EN VIN','Switched VBAT to 5V regulator'),
        ('A pin','D2 + D3 OR junction','Button SW + GPIO16 via 1N4148 - GND toggles power'),
        ('OFF pin','GPIO17','Pulse HIGH from ESP32 = forced shutdown'),
        ('B pin','Not connected','Leave unconnected'),
        ('GND (SW)','Button SW terminal 2','Button other terminal to GND'),
    ]):
        bg = ALT if i%2==0 else WHITE; pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[30,50,90])):
            pdf.set_font('Helvetica','B'if j==0 else'',7.5); pdf.cell(w,5,v,border=1,fill=True)
        pdf.set_fill_color(*WHITE); pdf.cell(20,5,'[]',border=1,fill=True,align='C'); pdf.ln()

    pdf.ln(3); pdf.sec('Resistor Checklist')
    pdf.tbl_hdr([('Ref',14),('Value',16),('Location',65),('Purpose',72),('Fitted',15)])
    for i,r in enumerate([
        ('R1','7.5k','Fill flow SIG to GPIO4','Signal divider series'),
        ('R2','15k','GPIO4 junction to GND','Signal divider shunt'),
        ('R3','7.5k','Drain flow SIG to GPIO5','Signal divider series'),
        ('R4','15k','GPIO5 junction to GND','Signal divider shunt'),
        ('R5','7.5k','Tank full SIG to GPIO6','Signal divider series'),
        ('R6','15k','GPIO6 junction to GND','Signal divider shunt'),
        ('R7','100k','VBAT+ to GPIO1','Battery divider series'),
        ('R8','33k','GPIO1 junction to GND','Battery divider shunt'),
        ('R9','4.7k','GPIO18 SDA to 3.3V','I2C0 SDA pull-up'),
        ('R10','4.7k','GPIO39 SCL to 3.3V','I2C0 SCL pull-up'),
        ('R11','15k','GPIO7 to 3.3V','Tank detect pull-up'),
        ('R12','100k','Pololu /OFF to GND (optional)','GPIO17 pull-down'),
    ]):
        bg = ALT if i%2==0 else WHITE; pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[14,16,65,72])):
            pdf.set_font('Helvetica','B'if j==0 else'',7.5)
            pdf.cell(w,5,v,border=1,fill=True,align='C'if j<2 else'L')
        pdf.set_fill_color(*WHITE); pdf.cell(15,5,'[]',border=1,fill=True,align='C'); pdf.ln()
    pdf.set_font('Helvetica','I',7); pdf.set_text_color(80,80,80)
    pdf.cell(0,4,'  Note: GT911 touch uses internal pull-ups on the display FPC - no external pull-ups needed on Wire1 (GPIO41/GPIO47).',new_x='LMARGIN',new_y='NEXT')
    pdf.set_text_color(*BLACK)

    pdf.ln(3); pdf.sec('Diode Checklist')
    pdf.tbl_hdr([('Ref',14),('Type',20),('Anode',44),('Cathode',34),('Purpose',56),('Fitted',14)])
    for i,r in enumerate([
        ('D2','1N4148','Encoder SW output','Pololu A pin','OR gate - button side'),
        ('D3','1N4148','GPIO16','Pololu A pin','OR gate - GPIO side'),
    ]):
        bg = ALT if i%2==0 else WHITE; pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[14,20,44,34,56])):
            pdf.set_font('Helvetica','B'if j==0 else'',7.5)
            pdf.cell(w,5,v,border=1,fill=True,align='C'if j<2 else'L')
        pdf.set_fill_color(*WHITE); pdf.cell(14,5,'[]',border=1,fill=True,align='C'); pdf.ln()

    pdf.ln(3); pdf.sec('Power Supply Checklist')
    pdf.tbl_hdr([('Item',45),('Specification',38),('Check before power-on',82),('Done',15)])
    for i,r in enumerate([
        ('5V regulator output','Set to 5.00V exactly','Measure with multimeter BEFORE connecting to board'),
        ('VBAT+ rail','9.0 - 12.6V','Correct polarity at XT60'),
        ('5V rail','5.0V +/-0.1V','Measure at ESP32 5V pin'),
        ('3.3V rail','3.3V +/-0.1V','Measure at ESP32 3V3 pin'),
        ('GND continuity','All GND points common','Check with continuity beeper'),
        ('20A fuse','Inline on VBAT+ to BTS','Fitted before motor testing'),
        ('BTS7960 heatsink','Mounted off-board','Aluminium bracket on case wall'),
    ]):
        bg = ALT if i%2==0 else WHITE; pdf.set_fill_color(*bg)
        for j,(v,w) in enumerate(zip(r,[45,38,82])):
            pdf.set_font('Helvetica','B'if j==0 else'',7.5); pdf.cell(w,5,v,border=1,fill=True)
        pdf.set_fill_color(*WHITE); pdf.cell(15,5,'[]',border=1,fill=True,align='C'); pdf.ln()

    pdf.ln(4)
    pdf.set_font('Helvetica','I',7); pdf.set_text_color(100,100,100)
    pdf.cell(0,4,'Button: SW+D2->Pololu A (GND toggles) | GPIO16+D3->A | Short press=firmware func | Long 3s+=GPIO17 HIGH=OFF | Rev 3.0',align='C',new_x='LMARGIN',new_y='NEXT')

    p = os.path.join(OUT,'ESP32_Pin_List_v2.pdf')
    pdf.output(p); print(f'Saved: {p}')


# ═══════════════════════════════════════════════════════
# CIRCUIT DIAGRAM  —  A2 Landscape  (matches V4 layout)
# ═══════════════════════════════════════════════════════
class Circuit(FPDF):
    def header(self):
        self.set_fill_color(*NAVY)
        self.rect(0,0,self.w,14,'F')
        self.set_font('Helvetica','B',12)
        self.set_text_color(*WHITE)
        self.set_xy(0,1)
        self.cell(0,7,'MCP Fuel Station v2  -  Circuit Diagram',align='C')
        self.set_font('Helvetica','',8)
        self.set_xy(0,8)
        self.cell(0,5,'ESP32-S3 N16R8  |  3S LiPo  |  MP1584EN 5V  |  Pololu SV Mini  |  BTS7960  |  ST7796S 3.5" IPS  |  DS3231 RTC  |  Honeywell ABP2  |  GT911 Touch  |  KY-040 Encoder',align='C')
        self.set_text_color(*BLACK)

    def footer(self):
        self.set_y(-8)
        self.set_font('Helvetica','',7)
        self.set_text_color(128,128,128)
        self.cell(0,5,f'MCP Fuel Station v2 - Circuit Diagram Rev 3.0  |  A2 Landscape  |  Page {self.page_no()}',align='C')
        self.set_text_color(*BLACK)

    def blk(self, x, y, w, h, title, t_bg, body_lines, font_size=7.5):
        self.set_fill_color(*t_bg)
        self.rect(x,y,w,6.5,'F')
        self.rect(x,y,w,h)
        self.set_font('Helvetica','B',8)
        self.set_text_color(*WHITE)
        self.set_xy(x+1,y+0.8)
        self.cell(w-2,5,title,align='C')
        self.set_text_color(*BLACK)
        ly = y+8
        for line in body_lines:
            if ly+3.5 > y+h: break
            bold = line.startswith('>') or line.startswith('--')
            self.set_font('Helvetica','B'if bold else'',font_size)
            line2 = line.lstrip('>').lstrip('-').strip() if bold else line
            self.set_xy(x+2,ly)
            self.cell(w-4,4,line2)
            ly+=4.1

    def sub(self, x, y, w, title, lines, font_size=7.5):
        h = 5.5 + len(lines)*4.1 + 2
        self.set_fill_color(180,210,240)
        self.rect(x,y,w,5.5,'F')
        self.rect(x,y,w,h)
        self.set_font('Helvetica','B',7.5)
        self.set_text_color(0,40,100)
        self.set_xy(x+1,y+0.8)
        self.cell(w-2,4.5,title)
        self.set_text_color(*BLACK)
        self.set_fill_color(240,248,255)
        self.rect(x,y+5.5,w,h-5.5,'F')
        ly = y+7
        for line in lines:
            self.set_font('Helvetica','',font_size)
            self.set_xy(x+2,ly)
            self.cell(w-4,4,line)
            ly+=4.1
        return h


def circuit_diagram():
    pdf = Circuit(unit='mm', format=(594, 420))  # A2 landscape: 594x420mm
    pdf.set_margins(4, 16, 4)
    pdf.set_auto_page_break(False)
    pdf.add_page()

    # A2 landscape = 594 x 420 mm
    PAGE_W = 594

    # ── Signal type legend ───────────────────────────────
    pdf.set_font('Helvetica','B',7)
    pdf.set_xy(4, 16.5)
    pdf.cell(27, 4, 'SIGNAL TYPES:')
    legend = [
        ('VBAT+ (9-12.6V)', (200,0,0)),
        ('5V regulated',     (180,80,0)),
        ('3.3V',             (0,130,0)),
        ('GND',              (0,0,0)),
        ('Signal/GPIO',      (0,80,180)),
        ('SPI bus',          (120,0,160)),
        ('I2C bus',          (0,140,140)),
    ]
    cx = 32
    for lbl, col in legend:
        pdf.set_fill_color(*col)
        pdf.rect(cx, 17.2, 7, 2.5, 'F')
        pdf.set_font('Helvetica','',6.5)
        pdf.set_xy(cx+8, 16.5)
        pdf.cell(len(lbl)*2.8+2, 4, lbl)
        cx += len(lbl)*2.8+12

    # ── Power rail lines ─────────────────────────────────
    rails = [
        ('VBAT+', 22.5, (200,0,0),   1.2),
        ('5V',    26.0, (180,80,0),  1.0),
        ('GND',   29.5, (0,0,0),     0.8),
    ]
    for label, ry, col, lw in rails:
        pdf.set_draw_color(*col); pdf.set_line_width(lw)
        pdf.line(4, ry, PAGE_W-4, ry)
        pdf.set_font('Helvetica','B',6); pdf.set_text_color(*col)
        pdf.set_xy(4, ry-3.5); pdf.cell(14, 3.5, label)
    pdf.set_text_color(*BLACK); pdf.set_line_width(0.2); pdf.set_draw_color(*BLACK)

    # ── Column layout ────────────────────────────────────
    # Left: 4-90, ESP32: 92-308, Shifters: 310-432, Right: 434-590
    LX, LW = 4, 86
    MX, MW = 92, 216
    SX, SW = 310, 122
    RX, RW = 434, 156
    TOP = 32

    # ── LEFT COLUMN ──────────────────────────────────────
    pdf.blk(LX, TOP,    LW, 28, '3S LiPo Battery  9.0-12.6V', NAVY, [
        'XT60 connector',
        '+ -> VBAT+ rail',
        '- -> GND rail',
    ])
    pdf.blk(LX, TOP+30, LW, 33, 'MP1584EN 5V Regulator', BLUE_HDR, [
        'VIN  <- VBAT+',
        'VOUT -> 5V rail',
        'GND  -> GND',
        '>Set to 5.00V before wiring!',
    ])
    pdf.blk(LX, TOP+65, LW, 52, 'Pololu SV Mini Power Switch', BLUE_HDR, [
        'VIN  <- VBAT+ (built-in rev. prot.)',
        'VOUT -> MP1584EN VIN',
        'A    <- D2+D3 OR gate (->GND toggles)',
        'OFF  <- GPIO17 (pulse HIGH=shutdown)',
        'B    -> not connected',
        '--',
        'Press A->GND: toggles power on/off',
        'GPIO17 HIGH: forced power off',
    ])
    pdf.blk(LX, TOP+119, LW, 33, 'Diode OR Gate - Pololu A pin', (80,80,80), [
        'D2 (1N4148): Encoder SW -> Pololu A',
        'D3 (1N4148): GPIO16    -> Pololu A',
        'Either pulling A LOW toggles power',
    ])
    pdf.blk(LX, TOP+154, LW, 68, 'Rotary Encoder KY-040', (0,100,60), [
        'GND -> GND rail',
        '+   <- 3.3V  (NOT 5V)',
        'SW  -> D2 1N4148 -> Pololu A',
        'SW also -> GPIO16 (reads timing)',
        'DT  -> GPIO21 (direction)',
        'CLK -> GPIO40 (clock)',
        '--',
        '>Remove R3 from encoder PCB',
        ' (eliminates SW pull-up to VCC)',
        'Short press = function select',
        'Long 3s+ = shutdown',
    ])
    pdf.blk(LX, TOP+224, LW, 22, 'Passive Piezo Buzzer', (80,80,80), [
        'GPIO3 -> + terminal',
        'GND   -> - terminal  (ledcWriteTone)',
    ])

    # ── ESP32 CENTRE BLOCK ───────────────────────────────
    ESP_H = 262
    pdf.set_fill_color(240,248,255)
    pdf.set_draw_color(*NAVY); pdf.set_line_width(0.6)
    pdf.rect(MX, TOP, MW, ESP_H, 'DF')
    pdf.set_line_width(0.2); pdf.set_draw_color(*BLACK)

    pdf.set_fill_color(*NAVY)
    pdf.rect(MX, TOP, MW, 8, 'F')
    pdf.set_font('Helvetica','B',9); pdf.set_text_color(*WHITE)
    pdf.set_xy(MX, TOP+1.5)
    pdf.cell(MW, 6, 'ESP32-S3 DevKitC-1 N16R8  -  240MHz  |  WiFi + BLE  |  16MB Flash  |  8MB PSRAM', align='C')
    pdf.set_text_color(*BLACK)

    # Three sub-columns inside ESP32
    C1 = MX+3;   C1W = 64
    C2 = MX+70;  C2W = 66
    C3 = MX+139; C3W = 74
    ey = TOP + 11

    # C1 — left sub-column
    pdf.sub(C1, ey,      C1W, 'POWER',
            ['5V  <- 5V rail','GND -> GND rail','3.3V -> sensors + display + touch'])
    pdf.sub(C1, ey+22,   C1W, 'POWER CONTROL',
            ['GPIO16 -> Pololu A via D3 1N4148','GPIO17 -> Pololu OFF (pulse HIGH)'])
    pdf.sub(C1, ey+44,   C1W, 'SPI2 - TFT DISPLAY',
            ['GPIO11 -> TFT SCL / SCK','GPIO12 -> TFT SDA / MOSI',
             'GPIO13 -> TFT CS','GPIO14 -> TFT DC',
             'GPIO15 -> TFT RES','GPIO48 -> TFT BL (PWM)'])
    pdf.sub(C1, ey+99,   C1W, 'I2C BUS 0 - Wire0 (4.7k PU)',
            ['GPIO18 SDA -> DS3231 + ABP2','GPIO39 SCL -> DS3231 + ABP2'])
    pdf.sub(C1, ey+121,  C1W, 'I2C BUS 1 - Wire1 [NEW]',
            ['GPIO47 SDA -> GT911 touch','GPIO41 SCL -> GT911 touch','GPIO42 INT <- GT911 touch'])
    pdf.sub(C1, ey+155,  C1W, 'ENCODER',
            ['GPIO40 <- CLK (clock)','GPIO21 <- DT  (direction)','GPIO16 <- SW  (via D3 diode)'])
    pdf.sub(C1, ey+178,  C1W, 'AUDIO',
            ['GPIO3 -> piezo buzzer (PWM)'])

    # C2 — middle sub-column
    pdf.sub(C2, ey,      C2W, 'SENSOR INPUTS',
            ['GPIO1  ADC <- battery divider (100k/33k)',
             'GPIO4  <- fill flow  (7.5k/15k divider)',
             'GPIO5  <- drain flow (7.5k/15k divider)',
             'GPIO6  <- tank full  (7.5k/15k divider)',
             'GPIO7  <- tank detect pin4 (15k pull-up)'])
    pdf.sub(C2, ey+55,   C2W, 'MOTOR DRIVER (BTS7960)',
            ['GPIO2  -> RPWM (fill PWM)','GPIO8  -> R_EN (hold HIGH)',
             'GPIO9  -> LPWM (drain PWM)','GPIO10 -> L_EN (hold HIGH)'])
    pdf.sub(C2, ey+99,   C2W, 'RESERVED - DO NOT USE',
            ['GPIO19/20  USB D-/D+ (kept free)',
             'GPIO26-33  internal flash','GPIO35-37  PSRAM'])
    pdf.sub(C2, ey+130,  C2W, 'SPARE GPIOs',
            ['GPIO38',
             'GPIO43/44 = UART0 TX/RX',
             'GPIO45/46 = strapping pins'])

    # C3 — right sub-column (connector summaries)
    pdf.sub(C3, ey,      C3W, 'CN1 Fill Flow (JR 3-pin)',
            ['Pin1 GND -> GND','Pin2 VCC <- 5V','Pin3 SIG -> 7.5k/15k -> GPIO4'])
    pdf.sub(C3, ey+35,   C3W, 'CN2 Drain Flow (JR 3-pin)',
            ['Pin1 GND -> GND','Pin2 VCC <- 5V','Pin3 SIG -> 7.5k/15k -> GPIO5'])
    pdf.sub(C3, ey+70,   C3W, 'CN3 Tank Full (4-pin)',
            ['Pin1 GND -> GND','Pin2 VCC <- 5V',
             'Pin3 SIG -> 7.5k/15k -> GPIO6','Pin4 DET -> 15k PU  -> GPIO7'])
    pdf.sub(C3, ey+110,  C3W, 'CN5 ABP2 Pressure (4-pin)',
            ['Pin1 GND -> GND','Pin2 VCC <- 3.3V',
             'Pin3 SDA <- GPIO18','Pin4 SCL <- GPIO39'])
    pdf.sub(C3, ey+150,  C3W, 'CN10 BTS7960 (6-pin Dupont)',
            ['Pin1 LPWM <- GPIO9','Pin2 L_EN <- GPIO10',
             'Pin3 R_EN <- GPIO8','Pin4 RPWM <- GPIO2',
             'Pin5 GND  -> GND','Pin6 5V   <- 5V rail'])

    # ── SIGNAL LEVEL SHIFTERS ────────────────────────────
    # Header bar
    pdf.set_fill_color(*NAVY)
    pdf.rect(SX, TOP, SW, 7, 'F')
    pdf.set_font('Helvetica','B',8); pdf.set_text_color(*WHITE)
    pdf.set_xy(SX+1, TOP+1.2)
    pdf.cell(SW-2, 5, 'SIGNAL LEVEL SHIFTERS  --  5V to 3.3V', align='C')
    pdf.set_text_color(*BLACK)

    def draw_divider(x, y, title, r1, r2, out_gpio, vin='5V'):
        """Draw a resistor voltage divider circuit schematic block."""
        BW = SW - 2
        # Section title bar
        pdf.set_fill_color(180,210,240)
        pdf.rect(x+1, y, BW, 5.5, 'F'); pdf.rect(x+1, y, BW, 5.5)
        pdf.set_font('Helvetica','B',7); pdf.set_text_color(0,40,100)
        pdf.set_xy(x+3, y+0.8); pdf.cell(BW-4, 4, title)
        pdf.set_text_color(*BLACK)

        # Pin text (left side)
        pdf.set_font('Helvetica','',6.5)
        pdf.set_xy(x+3, y+7);    pdf.cell(40, 3.5, 'Pin1 GND  ->  GND')
        pdf.set_xy(x+3, y+10.5); pdf.cell(40, 3.5, f'Pin2 VCC  <-  {vin}')
        pdf.set_xy(x+3, y+14);   pdf.cell(25, 3.5, 'Pin3 SIG  ->')

        # Resistor divider schematic (right side of block)
        rx = x + 52   # circuit x origin
        ry = y + 7    # circuit y origin

        # VCC label at top
        pdf.set_font('Helvetica','B',6.5); pdf.set_text_color(0,130,0)
        pdf.set_xy(rx-3, ry-0.5); pdf.cell(14, 3.5, vin, align='C')

        # Line: VCC -> top of R1
        pdf.set_draw_color(0,130,0); pdf.set_line_width(0.5)
        pdf.line(rx+4, ry+3, rx+4, ry+6)

        # R1 box (series, vertical)
        pdf.set_draw_color(0,0,0); pdf.set_line_width(0.3)
        pdf.rect(rx+1, ry+6, 7, 9)
        pdf.set_font('Helvetica','',5.5); pdf.set_text_color(0,0,0)
        pdf.set_xy(rx+1, ry+8.5); pdf.cell(7, 3, r1, align='C')

        # Line: R1 -> junction node
        pdf.set_draw_color(0,80,180); pdf.set_line_width(0.5)
        pdf.line(rx+4, ry+15, rx+4, ry+19)

        # Junction dot
        pdf.set_fill_color(0,80,180)
        pdf.ellipse(rx+2.5, ry+18.5, 3, 3, 'F')

        # R2 box (shunt, vertical)
        pdf.set_draw_color(0,0,0); pdf.set_line_width(0.3)
        pdf.line(rx+4, ry+21.5, rx+4, ry+24)
        pdf.rect(rx+1, ry+24, 7, 9)
        pdf.set_font('Helvetica','',5.5); pdf.set_text_color(0,0,0)
        pdf.set_xy(rx+1, ry+26.5); pdf.cell(7, 3, r2, align='C')

        # Line: R2 -> GND
        pdf.set_draw_color(0,0,0); pdf.set_line_width(0.3)
        pdf.line(rx+4, ry+33, rx+4, ry+36)

        # GND symbol (three horizontal lines)
        for i, ww in enumerate([8, 5, 2]):
            pdf.line(rx+4-ww/2, ry+36+i*2, rx+4+ww/2, ry+36+i*2)

        # Output tap: horizontal line from junction to GPIO label
        pdf.set_draw_color(0,80,180); pdf.set_line_width(0.5)
        pdf.line(rx+5.5, ry+20, rx+30, ry+20)

        # Arrow tip
        pdf.line(rx+30, ry+20, rx+27, ry+18)
        pdf.line(rx+30, ry+20, rx+27, ry+22)

        # GPIO label
        pdf.set_font('Helvetica','B',7); pdf.set_text_color(0,80,180)
        pdf.set_xy(rx+31, ry+17.5); pdf.cell(30, 4, '-> ' + out_gpio)

        pdf.set_text_color(*BLACK); pdf.set_draw_color(*BLACK); pdf.set_line_width(0.2)
        return y + 44   # height per divider block

    sy = TOP + 9
    sy = draw_divider(SX, sy,    'CN1  Fill Flow  (JR 3-pin)',   '7.5k', '15k', 'GPIO4')
    sy = draw_divider(SX, sy+2,  'CN2  Drain Flow  (JR 3-pin)',  '7.5k', '15k', 'GPIO5')
    sy = draw_divider(SX, sy+2,  'CN3  Tank Full  (4-pin)',       '7.5k', '15k', 'GPIO6')

    # CN3 Pin4 — sensor detect (15k pull-up to 3.3V)
    pdf.set_fill_color(180,210,240)
    pdf.rect(SX+1, sy+2, SW-2, 5.5, 'F'); pdf.rect(SX+1, sy+2, SW-2, 5.5)
    pdf.set_font('Helvetica','B',7); pdf.set_text_color(0,40,100)
    pdf.set_xy(SX+3, sy+2.8); pdf.cell(SW-6, 4, 'CN3  Pin4  SENSOR DETECT  (15k pull-up  ->  3.3V)')
    pdf.set_text_color(*BLACK); pdf.set_font('Helvetica','',6.5)
    pdf.set_xy(SX+3, sy+9.5);  pdf.cell(SW-6, 3.5, 'Pin4 DET: tied to GND inside connector shell')
    pdf.set_xy(SX+3, sy+13);   pdf.cell(SW-6, 3.5, '15k from 3.3V -> GPIO7  (pull-up to 3.3V)')
    pdf.set_xy(SX+3, sy+16.5); pdf.cell(SW-6, 3.5, 'DET grounds GPIO7 when sensor fitted  (LOW = fitted)')
    sy += 24

    # Battery voltage divider
    pdf.set_fill_color(180,210,240)
    pdf.rect(SX+1, sy+2, SW-2, 5.5, 'F'); pdf.rect(SX+1, sy+2, SW-2, 5.5)
    pdf.set_font('Helvetica','B',7); pdf.set_text_color(0,40,100)
    pdf.set_xy(SX+3, sy+2.8); pdf.cell(SW-6, 4, 'BATTERY VOLTAGE DIVIDER  ->  GPIO1 ADC')
    pdf.set_text_color(*BLACK)

    bx = SX + 14; by = sy + 10

    # VBAT+ label
    pdf.set_font('Helvetica','B',6.5); pdf.set_text_color(200,0,0)
    pdf.set_xy(bx-4, by-1); pdf.cell(16, 3.5, 'VBAT+', align='C')

    # VIN line
    pdf.set_draw_color(200,0,0); pdf.set_line_width(0.5)
    pdf.line(bx+4, by+3, bx+4, by+6)

    # R1 (100k)
    pdf.set_draw_color(0,0,0); pdf.set_line_width(0.3)
    pdf.rect(bx+1, by+6, 7, 10)
    pdf.set_font('Helvetica','',5.5); pdf.set_text_color(0,0,0)
    pdf.set_xy(bx+1, by+9); pdf.cell(7, 3, '100k', align='C')

    # Line to junction
    pdf.set_draw_color(0,80,180); pdf.set_line_width(0.5)
    pdf.line(bx+4, by+16, bx+4, by+20)

    # Junction dot
    pdf.set_fill_color(0,80,180)
    pdf.ellipse(bx+2.5, by+19.5, 3, 3, 'F')

    # R2 (33k)
    pdf.set_draw_color(0,0,0); pdf.set_line_width(0.3)
    pdf.line(bx+4, by+22.5, bx+4, by+25)
    pdf.rect(bx+1, by+25, 7, 10)
    pdf.set_font('Helvetica','',5.5); pdf.set_text_color(0,0,0)
    pdf.set_xy(bx+1, by+28); pdf.cell(7, 3, '33k', align='C')

    # Line to GND
    pdf.set_draw_color(0,0,0); pdf.set_line_width(0.3)
    pdf.line(bx+4, by+35, bx+4, by+38)
    for i, ww in enumerate([8, 5, 2]):
        pdf.line(bx+4-ww/2, by+38+i*2, bx+4+ww/2, by+38+i*2)

    # Output from junction
    pdf.set_draw_color(0,80,180); pdf.set_line_width(0.5)
    pdf.line(bx+5.5, by+21, bx+32, by+21)
    pdf.line(bx+32, by+21, bx+29, by+19)
    pdf.line(bx+32, by+21, bx+29, by+23)
    pdf.set_font('Helvetica','B',7); pdf.set_text_color(0,80,180)
    pdf.set_xy(bx+33, by+18.5); pdf.cell(40, 4, '-> GPIO1 ADC')
    pdf.set_font('Helvetica','',6.5); pdf.set_text_color(60,60,60)
    pdf.set_xy(bx+33, by+23);   pdf.cell(50, 3.5, '12.6V -> 3.13V max  (safe for ADC)')

    pdf.set_text_color(*BLACK); pdf.set_draw_color(*BLACK); pdf.set_line_width(0.2)

    # ── RIGHT COLUMN ─────────────────────────────────────
    ry = TOP
    pdf.blk(RX, ry, RW, 46, 'DS3231 RTC', BLUE_HDR, [
        'VCC <- 3.3V',
        'GND -> GND',
        'SDA <- GPIO18 (I2C0/Wire0)',
        'SCL <- GPIO39 (I2C0/Wire0)',
        'I2C addr: 0x68',
        'CR2032 coin cell backup',
        '>Remove charge resistor',
        ' if using CR2032 battery',
        '4.7k PU on SDA + SCL',
    ]); ry += 48

    pdf.blk(RX, ry, RW, 40, 'Honeywell ABP2 Pressure', BLUE_HDR, [
        'VCC <- 3.3V',
        'GND -> GND',
        'SDA <- GPIO18 (I2C0/Wire0)',
        'SCL <- GPIO39 (I2C0/Wire0)',
        'I2C addr: 0x28',
        '0-4 bar gauge pressure',
        'Barbed port - fit to fuel line',
        'Shared I2C0 with DS3231',
    ]); ry += 42

    pdf.blk(RX, ry, RW, 52, 'ST7796S 3.5" IPS Display  480x320', (0,120,80), [
        'GND -> GND rail',
        'VCC <- 3.3V',
        'SCL <- GPIO11 (SPI SCK)',
        'SDA <- GPIO12 (SPI MOSI)',
        'RES <- GPIO15 (reset)',
        'DC  <- GPIO14 (data/cmd)',
        'CS  <- GPIO13 (chip sel)',
        'BL  <- GPIO48 (backlight)',
        '8-pin FPC connector CN4',
        'Lib: TFT_eSPI  setRotation(1)',
    ]); ry += 54

    pdf.blk(RX, ry, RW, 58, 'GT911 Capacitive Touch  [NEW]', (0,100,160), [
        'VCC <- 3.3V (display FPC)',
        'GND -> GND',
        'SDA <- GPIO47 (Wire1/I2C1)',
        'SCL <- GPIO41 (Wire1/I2C1)',
        'INT -> GPIO42 (interrupt)',
        'RST -> not connected',
        'I2C addr: 0x5D',
        '>INT floats HIGH at power-up',
        ' -> default addr 0x5D',
        '>Poll every 50ms; no INT gating',
        'Status reg: 0x814E',
    ]); ry += 60

    pdf.blk(RX, ry, RW, 64, 'BTS7960 43A H-Bridge Motor Driver', (140,0,0), [
        'RPWM <- GPIO2  (fill PWM)',
        'R_EN <- GPIO8  (hold HIGH)',
        'LPWM <- GPIO9  (drain PWM)',
        'L_EN <- GPIO10 (hold HIGH)',
        'VCC  <- 5V rail',
        'GND  -> GND',
        'B+   <- VBAT+ via 20A fuse!',
        'B-   -> GND rail',
        'M+/M- -> pump motor CN7',
        '>Mount off-board on heatsink',
        '>20A fuse mandatory on B+',
    ])

    # ── CONNECTOR PINOUTS ────────────────────────────────
    CONN_Y = 298

    def draw_conn(x, y, title, pins):
        """pins = list of (num_label, signal, assignment)"""
        n = len(pins)
        PW = 12
        total_w = n * PW + 4

        pdf.set_fill_color(*TBL_HDR); pdf.set_text_color(*NAVY)
        pdf.set_font('Helvetica','B',6.5)
        pdf.rect(x, y, total_w, 5.5, 'F'); pdf.rect(x, y, total_w, 5.5)
        pdf.set_xy(x+1, y+0.8); pdf.cell(total_w-2, 4, title)
        pdf.set_text_color(*BLACK)

        for i, (num, sig, asgn) in enumerate(pins):
            px = x + 2 + i * PW
            py = y + 7
            pdf.set_fill_color(220,232,248)
            pdf.rect(px, py, PW-1, 10, 'DF')
            pdf.set_font('Helvetica','B',8)
            pdf.set_xy(px, py+1.5); pdf.cell(PW-1, 7, str(num), align='C')
            pdf.set_font('Helvetica','',5.5)
            pdf.set_xy(px-0.5, py+11);   pdf.cell(PW, 3.5, sig, align='C')
            pdf.set_font('Helvetica','B',5.5)
            pdf.set_xy(px-0.5, py+14.5); pdf.cell(PW, 3.5, asgn, align='C')

        return total_w

    gap = 6; cx = 4
    cx += draw_conn(cx, CONN_Y, 'CN1 Fill Flow', [
        (1,'SIG','GPIO4'),(2,'+ve','5V'),(3,'-ve','GND')]) + gap
    cx += draw_conn(cx, CONN_Y, 'CN2 Drain Flow', [
        (1,'SIG','GPIO5'),(2,'+ve','5V'),(3,'-ve','GND')]) + gap
    cx += draw_conn(cx, CONN_Y, 'CN3 Tank Full+Det', [
        (1,'SIG','GPIO6'),(2,'+ve','5V'),(3,'-ve','GND'),(4,'DET','GPIO7')]) + gap
    cx += draw_conn(cx, CONN_Y, 'CN5 ABP2 Pressure', [
        (1,'GND','GND'),(2,'VCC','3.3V'),(3,'SDA','GPIO18'),(4,'SCL','GPIO39')]) + gap
    cx += draw_conn(cx, CONN_Y, 'CN4 TFT Display (8-pin)', [
        (1,'BL','GPIO48'),(2,'CS','GPIO13'),(3,'DC','GPIO14'),(4,'RES','GPIO15'),
        (5,'SDA','GPIO12'),(6,'SCL','GPIO11'),(7,'VCC','3.3V'),(8,'GND','GND')]) + gap
    draw_conn(cx, CONN_Y, 'CN10 BTS7960 (6-pin)', [
        (1,'LPWM','GPIO9'),(2,'L_EN','GPIO10'),(3,'R_EN','GPIO8'),
        (4,'RPWM','GPIO2'),(5,'GND','GND'),(6,'5V','5V')])

    # ── BUILD NOTES ──────────────────────────────────────
    BN_Y = CONN_Y + 26
    BN_W = 520

    notes = [
        '1. Pre-set MP1584EN to 5.00V with multimeter BEFORE connecting to board.',
        '2. Remove R3 from encoder PCB - eliminates SW pull-up holding Pololu A HIGH.',
        '3. Remove DS3231 charge resistor (next to 1N4148 on back) before fitting CR2032.',
        '4. BTS7960 must be mounted off-board on aluminium heatsink - gets hot under load.',
        '5. 20A inline fuse is mandatory on VBAT+ to BTS7960 B+ terminal.',
        '6. Test all power rails with multimeter before inserting ESP32.',
        '7. Encoder: power from 3.3V ONLY - NOT 5V. Avoids 5V signals on ESP32 GPIO.',
        '8. I2C0 (Wire0): DS3231=0x68, ABP2=0x28. I2C1 (Wire1): GT911=0x5D. No address conflicts.',
        '9. GT911: Wire1 SDA=GPIO47, SCL=GPIO41, INT=GPIO42. No RST wired - INT floats HIGH at power-up -> addr 0x5D.',
        '10. Poll GT911 unconditionally every 50ms - do NOT gate on INT (stays LOW after init without RST).',
        '11. Display: ST7796S 3.5" IPS 480x320. TFT_eSPI: ST7796_DRIVER, USE_FSPI_PORT=1, setRotation(1).',
    ]

    pdf.set_fill_color(*TBL_HDR)
    pdf.rect(4, BN_Y, BN_W, 6, 'F'); pdf.rect(4, BN_Y, BN_W, 6)
    pdf.set_font('Helvetica','B',8); pdf.set_text_color(*NAVY)
    pdf.set_xy(6, BN_Y+1); pdf.cell(BN_W-4, 4, 'BUILD NOTES')
    pdf.set_text_color(*BLACK)

    nh = len(notes)*4.3 + 2
    pdf.set_fill_color(250,252,255)
    pdf.rect(4, BN_Y+6, BN_W, nh, 'F'); pdf.rect(4, BN_Y+6, BN_W, nh)
    ny = BN_Y + 8
    for note in notes:
        pdf.set_font('Helvetica','',6.5)
        pdf.set_xy(6, ny); pdf.cell(BN_W-4, 4, note)
        ny += 4.3

    # ── TITLE BLOCK ──────────────────────────────────────
    TB_X = 526; TB_Y = BN_Y; TB_W = 64; TB_H = nh + 8

    pdf.set_fill_color(*NAVY)
    pdf.rect(TB_X, TB_Y, TB_W, TB_H, 'DF')
    pdf.set_text_color(*WHITE)
    pdf.set_font('Helvetica','B',10)
    pdf.set_xy(TB_X+2, TB_Y+5)
    pdf.cell(TB_W-4, 7, 'MCP Fuel Station v2', align='C')
    pdf.set_font('Helvetica','',8)
    pdf.set_xy(TB_X+2, TB_Y+14)
    pdf.cell(TB_W-4, 5, 'Circuit Diagram Rev 3.0', align='C')
    pdf.set_font('Helvetica','',7)
    pdf.set_xy(TB_X+2, TB_Y+21)
    pdf.cell(TB_W-4, 5, 'ESP32-S3  Single board', align='C')
    pdf.set_xy(TB_X+2, TB_Y+28)
    pdf.cell(TB_W-4, 5, 'A2 Landscape', align='C')
    # Divider line
    pdf.set_draw_color(*WHITE); pdf.set_line_width(0.3)
    pdf.line(TB_X+4, TB_Y+35, TB_X+TB_W-4, TB_Y+35)
    pdf.set_line_width(0.2); pdf.set_draw_color(*BLACK)
    pdf.set_font('Helvetica','I',6)
    pdf.set_xy(TB_X+2, TB_Y+37)
    pdf.cell(TB_W-4, 4, 'Rev 2.0 -> Rev 3.0 changes:', align='C')
    pdf.set_font('Helvetica','',5.5)
    changes = ['ST7735S 1.8" -> ST7796S 3.5" IPS','GT911 touch added (Wire1)','GPIO41/42/47 now assigned']
    for i, c in enumerate(changes):
        pdf.set_xy(TB_X+2, TB_Y+42+i*5)
        pdf.cell(TB_W-4, 4, c, align='C')
    pdf.set_text_color(*BLACK)

    p = os.path.join(OUT,'circuit_diagram_v5.pdf')
    pdf.output(p); print(f'Saved: {p}')


if __name__=='__main__':
    pin_list()
    circuit_diagram()
    print('Done.')
