import csv

with open("/Users/mooneer/Downloads/NewFile1.csv") as csvfile:
    with open("/Users/mooneer/Downloads/usbinrush.csv", "w") as outfile:
        rdr = csv.reader(csvfile)
        wtr = csv.writer(outfile)
        i = 0
        for row in rdr:
            if i == 0:
                wtr.writerow(["Time", "A"])
            elif i == 1:
                pass     # Rigol CSV files use second row for header as well
            else:
                num = (float(row[0]) - 6000000) / 10000000
                #if num > 1000: break
                #elif num < -1000: continue
                volts = float(row[1])
                amps = volts #-volts #volts / 2.5    # 4 10ohm in parallel
                wtr.writerow([num, amps])
            i = i + 1
