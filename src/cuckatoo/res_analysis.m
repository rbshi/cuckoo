% evalution of the stability of cuckatoo solver

log = load('sol.log2-1');

len_log = size(log,2);
len_win = 5000;
stride_win = 100;

tmp = [];

for ii=1:stride_win:len_log-len_win+1
    tmp =[tmp, sum(log(:,ii:ii+len_win-1),2)];
end

tmp = tmp./len_win;

for ii=1:size(tmp,1)
    subplot(ceil(size(tmp,1)/3), 3, ii)
    plot(tmp(ii,:));
    ylim([0,0.1]);
    title(['Cycle Length = ' num2str(12+ii*4)])
end
