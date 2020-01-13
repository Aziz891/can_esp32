import pandas as pd
pid_dict = {"messages":[{"ID":96,"length":8,"data0":6,"data1":65,"data2":96,"data3":8,"data4":0,
"data5":0,"data6":0,"data7":0},{"ID":0,"length":8,"data0":6,"data1":65,"data2":0,"data3":190,
"data4":31,"data5":168,"data6":19,"data7":0},{"ID":32,"length":8,"data0":6,"data1":65,"data2":32,
"data3":144,"data4":5,"data5":176,"data6":21,"data7":0},{"ID":64,"length":8,"data0":6,"data1":65,
"data2":64,"data3":122,"data4":220,"data5":0,"data6":1,"data7":0}]}

data=[]
for i in range(1, 1+32*4):
  
    if i <= 32:
      temp =  ((pid_dict['messages'][1]['data3']<<24) + (pid_dict['messages'][1]['data4']<<16) + (pid_dict['messages'][1]['data5']<<8) + (pid_dict['messages'][1]['data6']) )
      result = (temp >> (32 - (i)))&1 
    
    elif i <= 64:
      temp =  ((pid_dict['messages'][2]['data3']<<24) + (pid_dict['messages'][2]['data4']<<16) + (pid_dict['messages'][2]['data5']<<8) + (pid_dict['messages'][2]['data6']) )
      result = (temp >> (64 - (i)))&1 
    elif i <= 96:
      temp =  ((pid_dict['messages'][3]['data3']<<24) + (pid_dict['messages'][3]['data4']<<16) + (pid_dict['messages'][3]['data5']<<8) + (pid_dict['messages'][3]['data6']) )
      result = (temp >> (96 - (i)))&1 
    
    elif i <= 128:
      temp =  ((pid_dict['messages'][0]['data3']<<24) + (pid_dict['messages'][0]['data4']<<16) + (pid_dict['messages'][0]['data5']<<8) + (pid_dict['messages'][0]['data6']) )
      result = (temp >> (128 - (i)))&1 
    
    
    data.append({'PID': i, 'Supported' : result})


excel_sheet = pd.DataFrame(data=data)
excel_sheet.to_csv(path_or_buf='output.csv')
pass



